#include "i2c_eeprom_emu.h"
#include "spi_flash_commands.h"
#include "rom_store.h"
#include "access_log.h"
#include "pin_config.h"

#include "driver/i2c_slave.h"
#include "esp_log.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <inttypes.h>

static const char *TAG = "i2c_emu";

/* Global stats accessible from SSE manager */
emu_stats_t g_i2c_stats;

typedef struct {
    i2c_slave_dev_handle_t  handle;
    const i2c_chip_info_t  *chip_info;
    uint16_t                addr_pointer;
    uint8_t                 addr_buf[2];
    uint8_t                 addr_bytes_received;
    bool                    running;
    TaskHandle_t            task;
} i2c_emu_ctx_t;

static i2c_emu_ctx_t s_ctx;

/*
 * I2C EEPROM emulation task.
 *
 * The ESP-IDF v5 I2C slave driver works with a TX/RX FIFO model.
 * We pre-fill the TX FIFO with data starting at the current address pointer.
 * On writes, we receive the address bytes first, then data.
 *
 * For the new ESP-IDF I2C slave API, we use callbacks for receive and
 * request events.
 */

static bool IRAM_ATTR i2c_slave_receive_cb(i2c_slave_dev_handle_t dev,
                                            const i2c_slave_rx_done_event_data_t *evt,
                                            void *arg)
{
    i2c_emu_ctx_t *ctx = (i2c_emu_ctx_t *)arg;
    rom_slot_t *slot = rom_store_get_active(BUS_I2C);
    if (!slot || !slot->data || !ctx->chip_info) return false;

    const uint8_t *rx_data = evt->buffer;
    uint32_t rx_len = evt->length;

    if (rx_len == 0) return false;

    uint8_t addr_bytes_needed = ctx->chip_info->addr_bytes;
    uint32_t offset = 0;

    /* First bytes are address */
    while (ctx->addr_bytes_received < addr_bytes_needed && offset < rx_len) {
        ctx->addr_buf[ctx->addr_bytes_received++] = rx_data[offset++];
    }

    if (ctx->addr_bytes_received >= addr_bytes_needed) {
        if (addr_bytes_needed == 2) {
            ctx->addr_pointer = ((uint16_t)ctx->addr_buf[0] << 8) | ctx->addr_buf[1];
        } else {
            ctx->addr_pointer = ctx->addr_buf[0];
        }
        /* Mask to chip size */
        ctx->addr_pointer %= ctx->chip_info->total_size;
    }

    /* Remaining bytes are data to write */
    if (offset < rx_len) {
        uint32_t data_len = rx_len - offset;
        uint32_t chip_size = ctx->chip_info->total_size;
        uint16_t page_size = ctx->chip_info->page_size;
        uint16_t page_start = ctx->addr_pointer & ~(page_size - 1);

        for (uint32_t i = 0; i < data_len; i++) {
            slot->data[ctx->addr_pointer] = rx_data[offset + i];
            /* Address increments within page, wraps at page boundary */
            ctx->addr_pointer = page_start | ((ctx->addr_pointer + 1) & (page_size - 1));
        }

        g_i2c_stats.total_writes++;
        g_i2c_stats.bytes_written += data_len;

        /* Log the write */
        access_log_entry_t log_entry = {
            .timestamp_ms = 0, /* Will be set by push_event or we use 0 for ISR */
            .address = ctx->addr_pointer - data_len,
            .length = (uint16_t)data_len,
            .bus = BUS_I2C,
            .operation = OP_WRITE,
            .command = 0,
            .slot = (uint8_t)rom_store_get_active_idx(BUS_I2C),
        };
        access_log_push(&log_entry);
    }

    /* Reset address byte counter for next transaction */
    ctx->addr_bytes_received = 0;

    return false; /* no high-prio task woken */
}

static bool IRAM_ATTR i2c_slave_request_cb(i2c_slave_dev_handle_t dev,
                                            const i2c_slave_rx_done_event_data_t *evt,
                                            void *arg)
{
    /* Master is reading - this callback may not exist in all IDF versions.
     * The TX FIFO approach is used instead. */
    return false;
}

/* Task that keeps the I2C TX FIFO fed with data at the current address */
static void i2c_emu_task(void *arg)
{
    i2c_emu_ctx_t *ctx = (i2c_emu_ctx_t *)arg;
    uint8_t tx_chunk[64];

    while (ctx->running) {
        rom_slot_t *slot = rom_store_get_active(BUS_I2C);
        if (!slot || !slot->data || !ctx->chip_info) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Pre-fill TX buffer with data starting at current address */
        uint32_t chip_size = ctx->chip_info->total_size;
        for (int i = 0; i < (int)sizeof(tx_chunk); i++) {
            tx_chunk[i] = slot->data[(ctx->addr_pointer + i) % chip_size];
        }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        uint32_t write_len = 0;
        esp_err_t err = i2c_slave_write(ctx->handle, tx_chunk, sizeof(tx_chunk),
                                         &write_len, pdMS_TO_TICKS(100));
#else
        esp_err_t err = i2c_slave_transmit(ctx->handle, tx_chunk, sizeof(tx_chunk),
                                            pdMS_TO_TICKS(100));
#endif
        if (err == ESP_OK) {
            g_i2c_stats.total_reads++;
            g_i2c_stats.bytes_read += sizeof(tx_chunk);
            ctx->addr_pointer = (ctx->addr_pointer + sizeof(tx_chunk)) % chip_size;

            access_log_push_event(BUS_I2C, OP_READ, 0,
                                  ctx->addr_pointer, sizeof(tx_chunk),
                                  rom_store_get_active_idx(BUS_I2C));
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    vTaskDelete(NULL);
}

esp_err_t i2c_eeprom_emu_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    memset(&g_i2c_stats, 0, sizeof(g_i2c_stats));
    ESP_LOGI(TAG, "I2C EEPROM emulator initialized");
    return ESP_OK;
}

esp_err_t i2c_eeprom_emu_start(chip_type_t chip)
{
    const i2c_chip_info_t *info = i2c_chip_find(chip);
    if (!info) {
        ESP_LOGE(TAG, "Unknown I2C chip type: %d", chip);
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ctx.running) {
        i2c_eeprom_emu_stop();
    }

    s_ctx.chip_info = info;
    s_ctx.addr_pointer = 0;
    s_ctx.addr_bytes_received = 0;

    /* Configure I2C slave */
    i2c_slave_config_t slave_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .send_buf_depth = 256,
        .receive_buf_depth = 256,
        .slave_addr = info->i2c_base_addr,
        .addr_bit_len = I2C_ADDR_BIT_LEN_7,
    };

    esp_err_t err = i2c_new_slave_device(&slave_cfg, &s_ctx.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C slave: %s", esp_err_to_name(err));
        return err;
    }

    /* Register receive callback */
    i2c_slave_event_callbacks_t cbs = {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        .on_receive = i2c_slave_receive_cb,
#else
        .on_recv_done = i2c_slave_receive_cb,
#endif
    };
    i2c_slave_register_event_callbacks(s_ctx.handle, &cbs, &s_ctx);

    /* Start the TX feeder task */
    s_ctx.running = true;
    xTaskCreatePinnedToCore(i2c_emu_task, "i2c_emu", 4096, &s_ctx, 20,
                            &s_ctx.task, 0);

    ESP_LOGI(TAG, "Started emulating %s (size=%" PRIu32 ", addr=0x%02X)",
             info->name, info->total_size, info->i2c_base_addr);
    return ESP_OK;
}

esp_err_t i2c_eeprom_emu_stop(void)
{
    if (!s_ctx.running) return ESP_OK;

    s_ctx.running = false;
    if (s_ctx.task) {
        vTaskDelay(pdMS_TO_TICKS(200)); /* Let the task exit */
        s_ctx.task = NULL;
    }

    if (s_ctx.handle) {
        i2c_del_slave_device(s_ctx.handle);
        s_ctx.handle = NULL;
    }

    ESP_LOGI(TAG, "I2C emulation stopped");
    return ESP_OK;
}

esp_err_t i2c_eeprom_emu_set_chip(chip_type_t chip)
{
    i2c_eeprom_emu_stop();
    return i2c_eeprom_emu_start(chip);
}

const emu_stats_t *i2c_eeprom_emu_get_stats(void)
{
    return &g_i2c_stats;
}

void i2c_eeprom_emu_reset_stats(void)
{
    memset(&g_i2c_stats, 0, sizeof(g_i2c_stats));
}
