#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "esp_timer.h"

#include "romemu_common.h"
#include "wifi_manager.h"
#include "rom_store.h"
#include "access_log.h"
#include "spi_flash_emu.h"
#include "i2c_eeprom_emu.h"
#include "web_server.h"
#include "sse_manager.h"
#include "gpio_control.h"

static const char *TAG = "romemu";

void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  ROMEMU - ESP32-S3 ROM Emulator v1.0");
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
        ESP_LOGE(TAG, "Make sure you have an ESP32-S3 module with PSRAM.");
    }

    /* 3. Initialize ROM store */
    ESP_ERROR_CHECK(rom_store_init());

    /* 4. Initialize access log */
    access_log_init();

    /* 5. Initialize WiFi */
    ESP_ERROR_CHECK(wifi_manager_init());

    /* 6. Initialize SSE manager (needs WiFi up) */
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
    ESP_LOGI(TAG, "  Web UI: http://%s/", wifi_manager_get_ip());
    ESP_LOGI(TAG, "  mDNS:   http://romemu.local/");
    ESP_LOGI(TAG, "  Mode:   %s", wifi_manager_is_ap_mode() ? "AP (connect to ROMEMU-xxxx)" : "STA");
    ESP_LOGI(TAG, "  Heap:   %u bytes free",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "  PSRAM:  %u bytes free",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "-------------------------------------------");
    ESP_LOGI(TAG, "Ready. Upload a ROM image via the web UI.");
    ESP_LOGI(TAG, "SPI pins: CS=10 CLK=12 MOSI=11 MISO=13 WP=14 HD=9");
    ESP_LOGI(TAG, "I2C pins: SDA=1 SCL=2");
    ESP_LOGI(TAG, "GPIO:     RESET=4 POWER=5");

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
