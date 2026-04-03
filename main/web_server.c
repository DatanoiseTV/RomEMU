#include "web_server.h"
#include "sse_manager.h"
#include "rom_store.h"
#include "spi_flash_emu.h"
#include "i2c_eeprom_emu.h"
#include "wifi_manager.h"
#include "access_log.h"
#include "spi_flash_commands.h"
#include "romemu_common.h"
#include "embedded_files.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "web";
static httpd_handle_t s_server = NULL;

/* ---- Handlers declared in web_handlers.c ---- */
extern esp_err_t handler_index(httpd_req_t *req);
extern esp_err_t handler_style(httpd_req_t *req);
extern esp_err_t handler_script(httpd_req_t *req);
extern esp_err_t handler_htmx(httpd_req_t *req);
extern esp_err_t handler_api_status(httpd_req_t *req);
extern esp_err_t handler_api_slots(httpd_req_t *req);
extern esp_err_t handler_api_slot_upload(httpd_req_t *req);
extern esp_err_t handler_api_slot_download(httpd_req_t *req);
extern esp_err_t handler_api_slot_insert(httpd_req_t *req);
extern esp_err_t handler_api_slot_eject(httpd_req_t *req);
extern esp_err_t handler_api_slot_delete(httpd_req_t *req);
extern esp_err_t handler_api_slot_label(httpd_req_t *req);
extern esp_err_t handler_api_chip_get(httpd_req_t *req);
extern esp_err_t handler_api_chip_set(httpd_req_t *req);
extern esp_err_t handler_api_stats(httpd_req_t *req);
extern esp_err_t handler_api_stats_reset(httpd_req_t *req);
extern esp_err_t handler_api_wifi_get(httpd_req_t *req);
extern esp_err_t handler_api_wifi_set(httpd_req_t *req);
extern esp_err_t handler_api_log(httpd_req_t *req);
extern esp_err_t handler_api_events(httpd_req_t *req);

/* Slot handler that extracts the slot number from the URI */
extern esp_err_t handler_api_slot_action(httpd_req_t *req);

/* ---- Route registration ---- */

static const httpd_uri_t uri_handlers[] = {
    { .uri = "/",               .method = HTTP_GET,    .handler = handler_index },
    { .uri = "/style.css",      .method = HTTP_GET,    .handler = handler_style },
    { .uri = "/app.js",         .method = HTTP_GET,    .handler = handler_script },
    { .uri = "/htmx.min.js",    .method = HTTP_GET,    .handler = handler_htmx },
    { .uri = "/api/status",     .method = HTTP_GET,    .handler = handler_api_status },
    { .uri = "/api/slots",      .method = HTTP_GET,    .handler = handler_api_slots },
    { .uri = "/api/chip",       .method = HTTP_GET,    .handler = handler_api_chip_get },
    { .uri = "/api/chip",       .method = HTTP_POST,   .handler = handler_api_chip_set },
    { .uri = "/api/stats",      .method = HTTP_GET,    .handler = handler_api_stats },
    { .uri = "/api/stats/reset",.method = HTTP_POST,   .handler = handler_api_stats_reset },
    { .uri = "/api/wifi",       .method = HTTP_GET,    .handler = handler_api_wifi_get },
    { .uri = "/api/wifi",       .method = HTTP_POST,   .handler = handler_api_wifi_set },
    { .uri = "/api/log",        .method = HTTP_GET,    .handler = handler_api_log },
    { .uri = "/api/events",     .method = HTTP_GET,    .handler = handler_api_events },
    /* Slot-specific endpoints use wildcard matching */
    { .uri = "/api/slots/*",    .method = HTTP_POST,   .handler = handler_api_slot_action },
    { .uri = "/api/slots/*",    .method = HTTP_GET,    .handler = handler_api_slot_download },
    { .uri = "/api/slots/*",    .method = HTTP_DELETE, .handler = handler_api_slot_delete },
};

#define URI_HANDLER_COUNT (sizeof(uri_handlers) / sizeof(uri_handlers[0]))

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = URI_HANDLER_COUNT + 4;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    for (int i = 0; i < (int)URI_HANDLER_COUNT; i++) {
        httpd_register_uri_handler(s_server, &uri_handlers[i]);
    }

    ESP_LOGI(TAG, "HTTP server started on port %d (%d handlers)",
             config.server_port, (int)URI_HANDLER_COUNT);
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    return ESP_OK;
}

httpd_handle_t web_server_get_handle(void)
{
    return s_server;
}
