#include "gpio_control.h"
#include "pin_config.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

static const char *TAG = "gpio_ctrl";

static bool s_reset_asserted = false;
static bool s_power_on = true;

esp_err_t gpio_control_init(void)
{
    /* Reset pin: open-drain output, default released (high-Z, pulled up by target) */
    gpio_config_t reset_cfg = {
        .pin_bit_mask = (1ULL << PIN_RESET_OUT),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&reset_cfg);
    gpio_set_level(PIN_RESET_OUT, 1); /* Released (not in reset) */
    s_reset_asserted = false;

    /* Power pin: push-pull output, default on */
    gpio_config_t power_cfg = {
        .pin_bit_mask = (1ULL << PIN_POWER_OUT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&power_cfg);
    gpio_set_level(PIN_POWER_OUT, 1); /* Power on */
    s_power_on = true;

    ESP_LOGI(TAG, "GPIO control initialized: RESET=GPIO%d POWER=GPIO%d",
             PIN_RESET_OUT, PIN_POWER_OUT);
    return ESP_OK;
}

void gpio_control_reset_assert(void)
{
    gpio_set_level(PIN_RESET_OUT, 0);
    s_reset_asserted = true;
    ESP_LOGI(TAG, "Reset ASSERTED (target held in reset)");
}

void gpio_control_reset_release(void)
{
    gpio_set_level(PIN_RESET_OUT, 1);
    s_reset_asserted = false;
    ESP_LOGI(TAG, "Reset RELEASED");
}

void gpio_control_reset_pulse(uint32_t duration_ms)
{
    if (duration_ms == 0) duration_ms = 100;
    ESP_LOGI(TAG, "Reset pulse: %u ms", duration_ms);
    gpio_control_reset_assert();
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_control_reset_release();
}

bool gpio_control_reset_is_asserted(void)
{
    return s_reset_asserted;
}

void gpio_control_power_on(void)
{
    gpio_set_level(PIN_POWER_OUT, 1);
    s_power_on = true;
    ESP_LOGI(TAG, "Power ON");
}

void gpio_control_power_off(void)
{
    gpio_set_level(PIN_POWER_OUT, 0);
    s_power_on = false;
    ESP_LOGI(TAG, "Power OFF");
}

bool gpio_control_power_is_on(void)
{
    return s_power_on;
}

void gpio_control_reset_sequence(uint32_t pre_delay_ms, uint32_t post_delay_ms)
{
    if (pre_delay_ms == 0) pre_delay_ms = 100;
    if (post_delay_ms == 0) post_delay_ms = 100;

    ESP_LOGI(TAG, "Reset sequence: assert -> %ums -> release -> %ums",
             pre_delay_ms, post_delay_ms);
    gpio_control_reset_assert();
    vTaskDelay(pdMS_TO_TICKS(pre_delay_ms));
    gpio_control_reset_release();
    vTaskDelay(pdMS_TO_TICKS(post_delay_ms));
}

void gpio_control_power_cycle(uint32_t off_time_ms, uint32_t on_settle_ms)
{
    if (off_time_ms == 0) off_time_ms = 500;
    if (on_settle_ms == 0) on_settle_ms = 200;

    ESP_LOGI(TAG, "Power cycle: off -> %ums -> on -> %ums settle",
             off_time_ms, on_settle_ms);
    gpio_control_power_off();
    vTaskDelay(pdMS_TO_TICKS(off_time_ms));
    gpio_control_power_on();
    vTaskDelay(pdMS_TO_TICKS(on_settle_ms));
}

int gpio_control_get_json(char *buf, size_t buf_size)
{
    return snprintf(buf, buf_size,
        "{\"reset_asserted\":%s,\"power_on\":%s,"
        "\"reset_gpio\":%d,\"power_gpio\":%d}",
        s_reset_asserted ? "true" : "false",
        s_power_on ? "true" : "false",
        PIN_RESET_OUT, PIN_POWER_OUT);
}
