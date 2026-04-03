#include "wifi_manager.h"

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi";

#define WIFI_STA_CONNECTED_BIT  BIT0
#define WIFI_STA_FAIL_BIT       BIT1
#define WIFI_STA_MAX_RETRY      5
#define WIFI_STA_TIMEOUT_MS     10000

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_count = 0;
static bool s_ap_mode = false;
static char s_ip_str[16] = "0.0.0.0";
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_STA_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "Retry STA connection (%d/%d)", s_retry_count, WIFI_STA_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_STA_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "STA connected: %s", s_ip_str);
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_STA_CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "AP: station " MACSTR " joined", MAC2STR(event->mac));
    }
}

static esp_err_t start_mdns(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) return err;
    mdns_hostname_set("romemu");
    mdns_instance_name_set("ESP32-S3 ROM Emulator");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    return ESP_OK;
}

static esp_err_t start_ap_mode(void)
{
    ESP_LOGI(TAG, "Starting AP mode");
    s_ap_mode = true;

    /* Get MAC for unique SSID */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    wifi_config_t ap_config = {
        .ap = {
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
            .channel = 1,
        },
    };
    snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid),
             "ROMEMU-%02X%02X", mac[4], mac[5]);
    ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    snprintf(s_ip_str, sizeof(s_ip_str), "192.168.4.1");
    ESP_LOGI(TAG, "AP mode active: SSID=%s IP=%s", ap_config.ap.ssid, s_ip_str);
    return ESP_OK;
}

static bool has_sta_credentials(wifi_config_t *sta_cfg)
{
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READONLY, &nvs) != ESP_OK) return false;

    size_t ssid_len = sizeof(sta_cfg->sta.ssid);
    size_t pass_len = sizeof(sta_cfg->sta.password);
    bool ok = (nvs_get_str(nvs, "ssid", (char *)sta_cfg->sta.ssid, &ssid_len) == ESP_OK &&
               ssid_len > 1);
    if (ok) {
        nvs_get_str(nvs, "pass", (char *)sta_cfg->sta.password, &pass_len);
    }
    nvs_close(nvs);
    return ok;
}

esp_err_t wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    /* Try STA mode first */
    wifi_config_t sta_cfg = {};
    if (has_sta_credentials(&sta_cfg)) {
        ESP_LOGI(TAG, "Trying STA: SSID=%s", sta_cfg.sta.ssid);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
        ESP_ERROR_CHECK(esp_wifi_start());

        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_STA_CONNECTED_BIT | WIFI_STA_FAIL_BIT,
            pdFALSE, pdFALSE,
            pdMS_TO_TICKS(WIFI_STA_TIMEOUT_MS));

        if (bits & WIFI_STA_CONNECTED_BIT) {
            ESP_LOGI(TAG, "STA mode connected");
            s_ap_mode = false;
            start_mdns();
            return ESP_OK;
        }

        ESP_LOGW(TAG, "STA connection failed, falling back to AP");
        esp_wifi_stop();
    } else {
        ESP_LOGI(TAG, "No STA credentials stored");
    }

    /* Fall back to AP mode */
    start_ap_mode();
    start_mdns();
    return ESP_OK;
}

esp_err_t wifi_manager_set_sta(const char *ssid, const char *password)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    nvs_set_str(nvs, "ssid", ssid);
    nvs_set_str(nvs, "pass", password ? password : "");
    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "WiFi credentials saved. Restarting WiFi...");

    /* Restart WiFi in STA mode */
    esp_wifi_stop();
    s_retry_count = 0;
    s_ap_mode = false;

    wifi_config_t sta_cfg = {};
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    if (password) {
        strncpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_STA_CONNECTED_BIT | WIFI_STA_FAIL_BIT,
        pdTRUE, pdFALSE,
        pdMS_TO_TICKS(WIFI_STA_TIMEOUT_MS));

    if (bits & WIFI_STA_CONNECTED_BIT) {
        return ESP_OK;
    }

    /* Failed - go back to AP */
    esp_wifi_stop();
    start_ap_mode();
    return ESP_ERR_TIMEOUT;
}

int wifi_manager_get_info_json(char *buf, size_t buf_size)
{
    wifi_ap_record_t ap;
    int rssi = 0;
    if (!s_ap_mode && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        rssi = ap.rssi;
    }
    return snprintf(buf, buf_size,
        "{\"mode\":\"%s\",\"ip\":\"%s\",\"rssi\":%d}",
        s_ap_mode ? "AP" : "STA", s_ip_str, rssi);
}

const char *wifi_manager_get_ip(void)
{
    return s_ip_str;
}

int8_t wifi_manager_get_rssi(void)
{
    if (s_ap_mode) return 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}

bool wifi_manager_is_ap_mode(void)
{
    return s_ap_mode;
}
