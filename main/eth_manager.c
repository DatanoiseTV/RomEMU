#include "eth_manager.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32P4

#include "pin_config.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "eth";

#define ETH_CONNECTED_BIT BIT0

static EventGroupHandle_t s_eth_event_group;
static bool s_eth_connected = false;
static char s_eth_ip[16] = "0.0.0.0";
static esp_eth_handle_t s_eth_handle = NULL;

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_base == ETH_EVENT) {
        switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Ethernet link up");
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Ethernet link down");
            s_eth_connected = false;
            snprintf(s_eth_ip, sizeof(s_eth_ip), "0.0.0.0");
            break;
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet started");
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet stopped");
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_eth_ip, sizeof(s_eth_ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Ethernet got IP: %s", s_eth_ip);
        s_eth_connected = true;
        if (s_eth_event_group) {
            xEventGroupSetBits(s_eth_event_group, ETH_CONNECTED_BIT);
        }
    }
}

esp_err_t eth_manager_init(void)
{
    s_eth_event_group = xEventGroupCreate();

    /* Register event handlers */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_event_handler, NULL, NULL));

    /* Create default netif for Ethernet */
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);

    /* Configure internal EMAC */
    eth_esp32_emac_config_t emac_cfg = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_cfg.smi_gpio.mdc_num = PIN_ETH_PHY_MDC;
    emac_cfg.smi_gpio.mdio_num = PIN_ETH_PHY_MDIO;
    emac_cfg.interface = EMAC_DATA_INTERFACE_RMII;
    emac_cfg.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_cfg.clock_config.rmii.clock_gpio = GPIO_NUM_50;

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_cfg);

    /* Configure IP101 PHY */
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr = 1;
    phy_cfg.reset_gpio_num = PIN_ETH_PHY_RST;

    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_cfg);

    /* Create Ethernet driver */
    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_err_t err = esp_eth_driver_install(&eth_cfg, &s_eth_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Attach to netif */
    esp_netif_attach(eth_netif, esp_eth_new_netif_glue(s_eth_handle));

    /* Start Ethernet */
    err = esp_eth_start(s_eth_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Ethernet initialized (IP101 PHY, RMII)");

    /* Wait briefly for link + DHCP */
    xEventGroupWaitBits(s_eth_event_group, ETH_CONNECTED_BIT,
                        pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));

    return ESP_OK;
}

const char *eth_manager_get_ip(void)
{
    return s_eth_ip;
}

bool eth_manager_is_connected(void)
{
    return s_eth_connected;
}

int eth_manager_get_info_json(char *buf, size_t buf_size)
{
    return snprintf(buf, buf_size,
        "{\"connected\":%s,\"ip\":\"%s\"}",
        s_eth_connected ? "true" : "false", s_eth_ip);
}

#else /* Non-P4 targets: stubs */

esp_err_t eth_manager_init(void) { return ESP_OK; }
const char *eth_manager_get_ip(void) { return "0.0.0.0"; }
bool eth_manager_is_connected(void) { return false; }
int eth_manager_get_info_json(char *buf, size_t buf_size) {
    return snprintf(buf, buf_size, "{\"connected\":false,\"ip\":\"0.0.0.0\"}");
}

#endif
