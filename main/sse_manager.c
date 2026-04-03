#include "sse_manager.h"
#include "access_log.h"
#include "rom_store.h"
#include "wifi_manager.h"
#include "romemu_common.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "sse";

#define SSE_TASK_STACK      4096
#define SSE_TASK_PRIO       5
#define SSE_POLL_MS         50
#define SSE_STATS_INTERVAL  2000   /* ms between stats events */
#define SSE_STATUS_INTERVAL 5000   /* ms between status events */
#define SSE_MAX_BATCH       20     /* max access events per poll */

/* External stats (defined in emulator modules) */
extern emu_stats_t g_spi_stats;
extern emu_stats_t g_i2c_stats;

typedef struct {
    int         fd;
    httpd_handle_t hd;
    bool        active;
} sse_client_t;

static sse_client_t s_clients[MAX_SSE_CLIENTS];
static int s_client_count = 0;
static TaskHandle_t s_task_handle = NULL;

static const char *op_names[] = { "read", "write", "erase", "cmd" };
static const char *bus_names[] = { "none", "spi", "i2c" };

static void send_to_all(const char *data, size_t len)
{
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (!s_clients[i].active) continue;

        esp_err_t err = httpd_socket_send(s_clients[i].hd, s_clients[i].fd,
                                          data, len, 0);
        if (err != ESP_OK) {
            ESP_LOGD(TAG, "SSE client %d disconnected", i);
            s_clients[i].active = false;
            s_client_count--;
        }
    }
}

static void send_access_events(void)
{
    access_log_entry_t entry;
    char buf[256];
    int count = 0;

    while (count < SSE_MAX_BATCH && access_log_pop(&entry)) {
        const char *op = (entry.operation < 4) ? op_names[entry.operation] : "?";
        const char *bus = (entry.bus < 3) ? bus_names[entry.bus] : "?";
        int n = snprintf(buf, sizeof(buf),
            "event: access\n"
            "data: {\"ts\":%u,\"addr\":\"0x%06X\",\"len\":%u,"
            "\"op\":\"%s\",\"bus\":\"%s\",\"cmd\":\"0x%02X\",\"slot\":%u}\n\n",
            entry.timestamp_ms, entry.address, entry.length,
            op, bus, entry.command, entry.slot);
        if (n > 0) send_to_all(buf, n);
        count++;
    }
}

static void send_stats_event(void)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "event: stats\n"
        "data: {\"spi_reads\":%llu,\"spi_writes\":%llu,\"spi_erases\":%llu,"
        "\"spi_bytes_read\":%llu,\"spi_bytes_written\":%llu,"
        "\"i2c_reads\":%llu,\"i2c_writes\":%llu,"
        "\"i2c_bytes_read\":%llu,\"i2c_bytes_written\":%llu}\n\n",
        g_spi_stats.total_reads, g_spi_stats.total_writes, g_spi_stats.total_erases,
        g_spi_stats.bytes_read, g_spi_stats.bytes_written,
        g_i2c_stats.total_reads, g_i2c_stats.total_writes,
        g_i2c_stats.bytes_read, g_i2c_stats.bytes_written);
    if (n > 0) send_to_all(buf, n);
}

static void send_status_event(void)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "event: status\n"
        "data: {\"heap_free\":%u,\"psram_free\":%u,"
        "\"uptime\":%llu,\"rssi\":%d,\"ip\":\"%s\","
        "\"ap_mode\":%s,\"sse_clients\":%d}\n\n",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        esp_timer_get_time() / 1000000ULL,
        wifi_manager_get_rssi(),
        wifi_manager_get_ip(),
        wifi_manager_is_ap_mode() ? "true" : "false",
        s_client_count);
    if (n > 0) send_to_all(buf, n);
}

static void sse_broadcast_task(void *arg)
{
    uint32_t last_stats = 0;
    uint32_t last_status = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SSE_POLL_MS));

        if (s_client_count == 0) continue;

        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        /* Stream access log entries */
        send_access_events();

        /* Periodic stats */
        if (now - last_stats >= SSE_STATS_INTERVAL) {
            send_stats_event();
            last_stats = now;
        }

        /* Periodic status */
        if (now - last_status >= SSE_STATUS_INTERVAL) {
            send_status_event();
            last_status = now;
        }
    }
}

void sse_manager_init(void)
{
    memset(s_clients, 0, sizeof(s_clients));
    s_client_count = 0;

    xTaskCreatePinnedToCore(sse_broadcast_task, "sse_bcast",
                            SSE_TASK_STACK, NULL, SSE_TASK_PRIO,
                            &s_task_handle, 0);
    ESP_LOGI(TAG, "SSE manager initialized");
}

esp_err_t sse_manager_add_client(httpd_req_t *req)
{
    /* Find a free slot */
    int idx = -1;
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (!s_clients[i].active) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        ESP_LOGW(TAG, "Max SSE clients reached");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Too many SSE clients");
        return ESP_ERR_NO_MEM;
    }

    /* Set SSE headers */
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    /* Send initial comment to establish connection */
    const char *hello = ": connected\n\n";
    httpd_resp_send_chunk(req, hello, strlen(hello));

    /* Store the socket fd for async sending */
    s_clients[idx].fd = httpd_req_to_sockfd(req);
    s_clients[idx].hd = req->handle;
    s_clients[idx].active = true;
    s_client_count++;

    ESP_LOGI(TAG, "SSE client %d connected (fd=%d), total=%d",
             idx, s_clients[idx].fd, s_client_count);

    /* Return ESP_OK but don't finalize the response - keep it open */
    return ESP_OK;
}

int sse_manager_client_count(void)
{
    return s_client_count;
}
