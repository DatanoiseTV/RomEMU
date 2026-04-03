#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "romemu_common.h"
#include "wifi_manager.h"
#include "eth_manager.h"
#include "rom_store.h"
#include "access_log.h"
#include "spi_flash_emu.h"
#include "i2c_eeprom_emu.h"
#include "web_server.h"
#include "sse_manager.h"
#include "gpio_control.h"
#include "pin_config.h"

static const char *TAG = "romemu";

#if CONFIG_IDF_TARGET_ESP32P4
#define TARGET_NAME "ESP32-P4"
#else
#define TARGET_NAME "ESP32-S3"
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  ROMEMU - %s ROM Emulator v1.1", TARGET_NAME);
    ESP_LOGI(TAG, "===========================================");

    /* 1. Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. Check PSRAM */
    size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM: %u bytes total, %u bytes free",
             (unsigned)psram_size,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (psram_size == 0) {
        ESP_LOGE(TAG, "ERROR: No PSRAM detected! ROM emulation requires PSRAM.");
    }

    /* 3. Initialize ROM store */
    ESP_ERROR_CHECK(rom_store_init());

    /* 4. Initialize access log */
    access_log_init();

    /* 5. Initialize networking */
#if CONFIG_IDF_TARGET_ESP32P4
    /* P4-Nano: Ethernet (IP101) is primary, WiFi via C6 is secondary */
    ESP_ERROR_CHECK(eth_manager_init());
    ESP_ERROR_CHECK(wifi_manager_init());
#else
    /* S3: WiFi only */
    ESP_ERROR_CHECK(wifi_manager_init());
#endif

    /* 6. Initialize SSE manager */
    sse_manager_init();

    /* 7. Start HTTP server */
    ESP_ERROR_CHECK(web_server_start());

    /* 8. Initialize GPIO control (reset + power) */
    ESP_ERROR_CHECK(gpio_control_init());

    /* 9. Initialize emulators (hardware peripherals) */
    ESP_ERROR_CHECK(spi_flash_emu_init());
    ESP_ERROR_CHECK(i2c_eeprom_emu_init());

    /* Log startup info */
    ESP_LOGI(TAG, "-------------------------------------------");
#if CONFIG_IDF_TARGET_ESP32P4
    if (eth_manager_is_connected()) {
        ESP_LOGI(TAG, "  Ethernet: http://%s/", eth_manager_get_ip());
    }
#endif
    ESP_LOGI(TAG, "  WiFi:   http://%s/", wifi_manager_get_ip());
    ESP_LOGI(TAG, "  mDNS:   http://romemu.local/");
    ESP_LOGI(TAG, "  Mode:   %s", wifi_manager_is_ap_mode() ? "AP (connect to ROMEMU-xxxx)" : "STA");
    ESP_LOGI(TAG, "  Heap:   %u bytes free",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "  PSRAM:  %u bytes free",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "-------------------------------------------");
    ESP_LOGI(TAG, "Ready. Upload a ROM image via the web UI.");
    ESP_LOGI(TAG, "SPI pins: CS=%d CLK=%d MOSI=%d MISO=%d WP=%d HD=%d",
             PIN_SPI_CS, PIN_SPI_CLK, PIN_SPI_MOSI, PIN_SPI_MISO,
             PIN_SPI_WP, PIN_SPI_HD);
    ESP_LOGI(TAG, "I2C pins: SDA=%d SCL=%d", PIN_I2C_SDA, PIN_I2C_SCL);
    ESP_LOGI(TAG, "GPIO:     RESET=%d POWER=%d", PIN_RESET_OUT, PIN_POWER_OUT);

    /* Main loop - watchdog + periodic status */
    uint32_t last_status_print = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));

        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000);
        if (now - last_status_print >= 60) {
            ESP_LOGI(TAG, "Status: heap=%u psram=%u spi_slot=%d i2c_slot=%d",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     rom_store_get_active_idx(BUS_SPI),
                     rom_store_get_active_idx(BUS_I2C));
            last_status_print = now;
        }
    }
}
