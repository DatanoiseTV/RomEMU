#include "spi_flash_emu.h"
#include "spi_flash_commands.h"
#include "rom_store.h"
#include "compressed_store.h"
#include "access_log.h"
#include "pin_config.h"

#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <inttypes.h>

static const char *TAG = "spi_emu";

/* Global stats accessible from SSE manager */
emu_stats_t g_spi_stats;

/*
 * SPI transaction buffer size.
 * We use a large buffer to handle full reads in one transaction.
 * The master can clock out up to this many bytes per CS assertion.
 */
#define SPI_BUF_SIZE    4096
#define SPI_DMA_CHAN     SPI_DMA_CH_AUTO

typedef struct {
    const spi_chip_info_t  *chip_info;
    bool                    running;
    TaskHandle_t            task;

    /* Flash state */
    bool        write_enable;
    bool        four_byte_mode;     /* for W25Q256/512 */
    uint8_t     status_reg1;
    uint8_t     status_reg2;
    uint8_t     status_reg3;
    bool        powered_down;

    /* DMA buffers - must be DMA-capable (internal SRAM or PSRAM with DMA) */
    uint8_t    *rx_buf;
    uint8_t    *tx_buf;
} spi_emu_ctx_t;

static spi_emu_ctx_t s_ctx;

/* ---- Flash command handlers ---- */

static inline uint32_t decode_address(const uint8_t *buf, bool four_byte)
{
    if (four_byte) {
        return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
               ((uint32_t)buf[2] << 8) | buf[3];
    }
    return ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
}

static void prepare_jedec_id_response(uint8_t *tx)
{
    if (!s_ctx.chip_info) return;
    tx[0] = 0xFF; /* dummy during command byte */
    tx[1] = s_ctx.chip_info->jedec_manufacturer;
    tx[2] = s_ctx.chip_info->jedec_memory_type;
    tx[3] = s_ctx.chip_info->jedec_capacity;
}

static void prepare_status_response(uint8_t *tx, uint8_t reg)
{
    tx[0] = 0xFF; /* dummy during command byte */
    switch (reg) {
        case 1: tx[1] = s_ctx.status_reg1; break;
        case 2: tx[1] = s_ctx.status_reg2; break;
        case 3: tx[1] = s_ctx.status_reg3; break;
    }
}

static void handle_read_command(const uint8_t *rx, uint32_t rx_len,
                                uint8_t *tx, uint32_t tx_size, uint8_t cmd)
{
    rom_slot_t *slot = rom_store_get_active(BUS_SPI);
    if (!slot || (!slot->data && !slot->cstore) || !s_ctx.chip_info) return;

    bool use_4byte = s_ctx.four_byte_mode || s_ctx.chip_info->four_byte_addr;
    int addr_len = use_4byte ? 4 : 3;
    int dummy_bytes = 0;

    /* Determine header length based on command */
    switch (cmd) {
        case SPI_CMD_READ_DATA:
        case SPI_CMD_READ_DATA_4B:
            dummy_bytes = 0;
            break;
        case SPI_CMD_FAST_READ:
        case SPI_CMD_FAST_READ_4B:
        case SPI_CMD_DUAL_READ:
        case SPI_CMD_QUAD_READ:
            dummy_bytes = 1;
            break;
        case SPI_CMD_DUAL_IO_READ:
            dummy_bytes = 1;
            break;
        case SPI_CMD_QUAD_IO_READ:
            dummy_bytes = 3; /* mode + 2 dummy in quad mode */
            break;
    }

    if (cmd == SPI_CMD_READ_DATA_4B || cmd == SPI_CMD_FAST_READ_4B) {
        addr_len = 4;
    }

    int header_len = 1 + addr_len + dummy_bytes; /* cmd + addr + dummy */

    if (rx_len < (uint32_t)header_len) return;

    uint32_t addr = decode_address(rx + 1, addr_len == 4);
    uint32_t chip_size = s_ctx.chip_info->total_size;
    addr %= chip_size;

    uint32_t data_len = rx_len - header_len;
    if (data_len > tx_size - header_len) {
        data_len = tx_size - header_len;
    }

    /* Read ROM data into TX buffer using compressed or raw store */
    if (slot->compressed && slot->cstore) {
        cstore_read(slot->cstore, addr, tx + header_len, data_len);
    } else if (slot->data) {
        uint32_t alloc = slot->alloc_size;
        for (uint32_t i = 0; i < data_len; i++) {
            uint32_t eff_addr = (addr + i) % chip_size;
            tx[header_len + i] = (eff_addr < alloc) ? slot->data[eff_addr] : 0xFF;
        }
    } else {
        memset(tx + header_len, 0xFF, data_len);
    }

    g_spi_stats.total_reads++;
    g_spi_stats.bytes_read += data_len;

    access_log_push_event(BUS_SPI, OP_READ, cmd, addr, (uint16_t)data_len,
                          rom_store_get_active_idx(BUS_SPI));
}

static void handle_page_program(const uint8_t *rx, uint32_t rx_len, uint8_t cmd)
{
    if (!s_ctx.write_enable) return;

    rom_slot_t *slot = rom_store_get_active(BUS_SPI);
    if (!slot || (!slot->data && !slot->cstore) || !s_ctx.chip_info) return;

    bool use_4byte = (cmd == SPI_CMD_PAGE_PROGRAM_4B) ||
                     s_ctx.four_byte_mode || s_ctx.chip_info->four_byte_addr;
    int addr_len = use_4byte ? 4 : 3;
    int header_len = 1 + addr_len;

    if (rx_len <= (uint32_t)header_len) return;

    uint32_t addr = decode_address(rx + 1, addr_len == 4);
    uint32_t chip_size = s_ctx.chip_info->total_size;
    uint16_t page_size = s_ctx.chip_info->page_size;
    addr %= chip_size;

    uint32_t data_len = rx_len - header_len;
    uint32_t page_start = addr & ~((uint32_t)page_size - 1);

    /* Page program: can only clear bits (AND), wraps within page */
    if (slot->compressed && slot->cstore) {
        /* Write through compressed store with page wrap */
        for (uint32_t i = 0; i < data_len; i++) {
            uint32_t write_addr = page_start | ((addr + i) & (page_size - 1));
            cstore_write(slot->cstore, write_addr, &rx[header_len + i], 1, true);
        }
    } else if (slot->data) {
        uint32_t alloc = slot->alloc_size;
        for (uint32_t i = 0; i < data_len; i++) {
            uint32_t write_addr = page_start | ((addr + i) & (page_size - 1));
            if (write_addr < alloc) {
                slot->data[write_addr] &= rx[header_len + i];
            }
        }
    }

    s_ctx.write_enable = false;
    g_spi_stats.total_writes++;
    g_spi_stats.bytes_written += data_len;

    access_log_push_event(BUS_SPI, OP_WRITE, cmd, addr, (uint16_t)data_len,
                          rom_store_get_active_idx(BUS_SPI));
}

static void handle_erase(const uint8_t *rx, uint32_t rx_len, uint8_t cmd)
{
    if (!s_ctx.write_enable) return;

    rom_slot_t *slot = rom_store_get_active(BUS_SPI);
    if (!slot || (!slot->data && !slot->cstore) || !s_ctx.chip_info) return;

    uint32_t chip_size = s_ctx.chip_info->total_size;
    uint32_t erase_addr = 0;
    uint32_t erase_size = 0;

    if (cmd == SPI_CMD_CHIP_ERASE || cmd == SPI_CMD_CHIP_ERASE_ALT) {
        /* Chip erase */
        erase_addr = 0;
        erase_size = chip_size;
    } else {
        bool use_4byte = (cmd == SPI_CMD_SECTOR_ERASE_4B || cmd == SPI_CMD_BLOCK_ERASE_64K_4B) ||
                         s_ctx.four_byte_mode || s_ctx.chip_info->four_byte_addr;
        int addr_len = use_4byte ? 4 : 3;
        if (rx_len < (uint32_t)(1 + addr_len)) return;

        erase_addr = decode_address(rx + 1, addr_len == 4);
        erase_addr %= chip_size;

        switch (cmd) {
            case SPI_CMD_SECTOR_ERASE:
            case SPI_CMD_SECTOR_ERASE_4B:
                erase_size = s_ctx.chip_info->sector_size;
                erase_addr &= ~(erase_size - 1);
                break;
            case SPI_CMD_BLOCK_ERASE_32K:
                erase_size = 32 * 1024;
                erase_addr &= ~(erase_size - 1);
                break;
            case SPI_CMD_BLOCK_ERASE_64K:
            case SPI_CMD_BLOCK_ERASE_64K_4B:
                erase_size = s_ctx.chip_info->block_size;
                erase_addr &= ~(erase_size - 1);
                break;
        }
    }

    if (erase_size > 0) {
        /* Erase = set all bytes to 0xFF */
        if (slot->compressed && slot->cstore) {
            cstore_erase(slot->cstore, erase_addr, erase_size);
        } else if (slot->data) {
            uint32_t alloc = slot->alloc_size;
            uint32_t start = erase_addr;
            uint32_t end = erase_addr + erase_size;
            if (start < alloc) {
                if (end > alloc) end = alloc;
                memset(&slot->data[start], 0xFF, end - start);
            }
        }

        s_ctx.write_enable = false;
        g_spi_stats.total_erases++;

        access_log_push_event(BUS_SPI, OP_ERASE, cmd, erase_addr,
                              (uint16_t)(erase_size > 0xFFFF ? 0xFFFF : erase_size),
                              rom_store_get_active_idx(BUS_SPI));
    }
}

static void handle_read_sfdp(const uint8_t *rx, uint32_t rx_len,
                              uint8_t *tx, uint32_t tx_size)
{
    /*
     * Minimal SFDP table: just enough to tell the host about basic
     * capabilities. Real flash chips have much larger SFDP tables,
     * but for coreboot/flashrom compatibility, this minimal one works.
     */
    static const uint8_t sfdp_header[] = {
        0x53, 0x46, 0x44, 0x50, /* "SFDP" signature */
        0x06,                    /* Minor revision 6 */
        0x01,                    /* Major revision 1 */
        0x01,                    /* Number of parameter headers - 1 */
        0xFF,                    /* Unused */
        /* Parameter header 0 (JEDEC Basic Flash Parameter) */
        0x00,                    /* ID LSB */
        0x05,                    /* Minor revision */
        0x01,                    /* Major revision */
        0x10,                    /* Length in DWORDs (16) */
        0x80, 0x00, 0x00,       /* Table pointer (0x000080) */
        0xFF,                    /* ID MSB */
    };

    int header_len = 1 + 3 + 1; /* cmd + 3 addr + 1 dummy */
    if (rx_len < (uint32_t)header_len) return;

    uint32_t addr = decode_address(rx + 1, false);
    uint32_t data_len = rx_len - header_len;

    for (uint32_t i = 0; i < data_len && i < tx_size - header_len; i++) {
        uint32_t sfdp_off = addr + i;
        if (sfdp_off < sizeof(sfdp_header)) {
            tx[header_len + i] = sfdp_header[sfdp_off];
        } else {
            tx[header_len + i] = 0xFF;
        }
    }

    access_log_push_event(BUS_SPI, OP_CMD, SPI_CMD_READ_SFDP, addr,
                          (uint16_t)data_len, rom_store_get_active_idx(BUS_SPI));
}

/* ---- Main SPI transaction processing ---- */

static void process_transaction(const uint8_t *rx, uint32_t rx_len,
                                uint8_t *tx, uint32_t tx_size)
{
    if (rx_len == 0) return;

    uint8_t cmd = rx[0];

    switch (cmd) {
    /* Read commands */
    case SPI_CMD_READ_DATA:
    case SPI_CMD_FAST_READ:
    case SPI_CMD_DUAL_READ:
    case SPI_CMD_QUAD_READ:
    case SPI_CMD_DUAL_IO_READ:
    case SPI_CMD_QUAD_IO_READ:
    case SPI_CMD_READ_DATA_4B:
    case SPI_CMD_FAST_READ_4B:
        handle_read_command(rx, rx_len, tx, tx_size, cmd);
        break;

    /* JEDEC ID */
    case SPI_CMD_READ_JEDEC_ID:
        prepare_jedec_id_response(tx);
        access_log_push_event(BUS_SPI, OP_CMD, cmd, 0, 3,
                              rom_store_get_active_idx(BUS_SPI));
        break;

    /* Status registers */
    case SPI_CMD_READ_STATUS_1:
        prepare_status_response(tx, 1);
        break;
    case SPI_CMD_READ_STATUS_2:
        prepare_status_response(tx, 2);
        break;
    case SPI_CMD_READ_STATUS_3:
        prepare_status_response(tx, 3);
        break;

    /* Write enable/disable */
    case SPI_CMD_WRITE_ENABLE:
        s_ctx.write_enable = true;
        s_ctx.status_reg1 |= 0x02; /* WEL bit */
        access_log_push_event(BUS_SPI, OP_CMD, cmd, 0, 0,
                              rom_store_get_active_idx(BUS_SPI));
        break;
    case SPI_CMD_WRITE_DISABLE:
        s_ctx.write_enable = false;
        s_ctx.status_reg1 &= ~0x02;
        access_log_push_event(BUS_SPI, OP_CMD, cmd, 0, 0,
                              rom_store_get_active_idx(BUS_SPI));
        break;

    /* Write status registers */
    case SPI_CMD_WRITE_STATUS_1:
        if (s_ctx.write_enable && rx_len >= 2) {
            s_ctx.status_reg1 = rx[1];
            s_ctx.write_enable = false;
        }
        break;
    case SPI_CMD_WRITE_STATUS_2:
        if (s_ctx.write_enable && rx_len >= 2) {
            s_ctx.status_reg2 = rx[1];
            s_ctx.write_enable = false;
        }
        break;
    case SPI_CMD_WRITE_STATUS_3:
        if (s_ctx.write_enable && rx_len >= 2) {
            s_ctx.status_reg3 = rx[1];
            s_ctx.write_enable = false;
        }
        break;

    /* Page program */
    case SPI_CMD_PAGE_PROGRAM:
    case SPI_CMD_PAGE_PROGRAM_4B:
        handle_page_program(rx, rx_len, cmd);
        break;

    /* Erase */
    case SPI_CMD_SECTOR_ERASE:
    case SPI_CMD_SECTOR_ERASE_4B:
    case SPI_CMD_BLOCK_ERASE_32K:
    case SPI_CMD_BLOCK_ERASE_64K:
    case SPI_CMD_BLOCK_ERASE_64K_4B:
    case SPI_CMD_CHIP_ERASE:
    case SPI_CMD_CHIP_ERASE_ALT:
        handle_erase(rx, rx_len, cmd);
        break;

    /* SFDP */
    case SPI_CMD_READ_SFDP:
        handle_read_sfdp(rx, rx_len, tx, tx_size);
        break;

    /* 4-byte address mode */
    case SPI_CMD_ENTER_4BYTE_MODE:
        s_ctx.four_byte_mode = true;
        access_log_push_event(BUS_SPI, OP_CMD, cmd, 0, 0,
                              rom_store_get_active_idx(BUS_SPI));
        break;
    case SPI_CMD_EXIT_4BYTE_MODE:
        s_ctx.four_byte_mode = false;
        access_log_push_event(BUS_SPI, OP_CMD, cmd, 0, 0,
                              rom_store_get_active_idx(BUS_SPI));
        break;

    /* Device ID / Release from power-down (both are opcode 0xAB) */
    case SPI_CMD_RELEASE_PD:
        s_ctx.powered_down = false;
        if (s_ctx.chip_info && rx_len >= 4) {
            tx[4] = s_ctx.chip_info->jedec_capacity;
        }
        break;

    /* Power down */
    case SPI_CMD_POWER_DOWN:
        s_ctx.powered_down = true;
        break;

    /* Reset */
    case SPI_CMD_ENABLE_RESET:
        break;
    case SPI_CMD_RESET:
        s_ctx.write_enable = false;
        s_ctx.four_byte_mode = false;
        s_ctx.powered_down = false;
        s_ctx.status_reg1 = 0;
        s_ctx.status_reg2 = 0;
        s_ctx.status_reg3 = 0;
        break;

    /* Read Unique ID */
    case SPI_CMD_READ_UNIQUE_ID:
        /* 1 cmd + 4 dummy + 8 bytes unique ID */
        if (tx_size >= 13) {
            /* Return a fake but consistent unique ID */
            tx[5]  = 0x52; tx[6]  = 0x4F; tx[7]  = 0x4D;  /* "ROM" */
            tx[8]  = 0x45; tx[9]  = 0x4D; tx[10] = 0x55;  /* "EMU" */
            tx[11] = 0x01; tx[12] = 0x00;
        }
        break;

    default:
        ESP_LOGD(TAG, "Unknown SPI command: 0x%02X", cmd);
        break;
    }
}

/*
 * Pre-transaction callback (ISR context).
 * Called before the SPI slave transaction starts.
 * We pre-fill the TX buffer with JEDEC ID at known offsets so that
 * for short commands (JEDEC ID, status), the response is already there.
 */
static void IRAM_ATTR spi_post_setup_cb(spi_slave_transaction_t *trans)
{
    /* Pre-fill common responses so they're ready on first clock edges */
    /* This callback runs after the transaction is set up but before
     * data transfer begins. We can't read RX data yet. */
}

/*
 * Post-transaction callback (ISR context).
 * Called after CS is deasserted and the transaction is complete.
 */
static void IRAM_ATTR spi_post_trans_cb(spi_slave_transaction_t *trans)
{
    /* Actual processing happens in the task, not in ISR */
}

/* ---- SPI emulation task (runs on Core 1 for lowest latency) ---- */

static void spi_emu_task(void *arg)
{
    spi_emu_ctx_t *ctx = (spi_emu_ctx_t *)arg;
    spi_slave_transaction_t trans;

    while (ctx->running) {
        memset(&trans, 0, sizeof(trans));
        memset(ctx->tx_buf, 0xFF, SPI_BUF_SIZE);

        /* Pre-fill JEDEC ID response at offset 1 (after command byte) */
        if (ctx->chip_info) {
            ctx->tx_buf[1] = ctx->chip_info->jedec_manufacturer;
            ctx->tx_buf[2] = ctx->chip_info->jedec_memory_type;
            ctx->tx_buf[3] = ctx->chip_info->jedec_capacity;
        }

        trans.length = SPI_BUF_SIZE * 8;    /* bits */
        trans.tx_buffer = ctx->tx_buf;
        trans.rx_buffer = ctx->rx_buf;

        esp_err_t err = spi_slave_transmit(SPI2_HOST, &trans, pdMS_TO_TICKS(100));
        if (err == ESP_ERR_TIMEOUT) continue;
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPI slave transmit error: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        uint32_t rx_len = trans.trans_len / 8;  /* bits to bytes */
        if (rx_len == 0) continue;

        /* Process the transaction */
        process_transaction(ctx->rx_buf, rx_len, ctx->tx_buf, SPI_BUF_SIZE);
    }

    vTaskDelete(NULL);
}

/* ---- Public API ---- */

esp_err_t spi_flash_emu_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    memset(&g_spi_stats, 0, sizeof(g_spi_stats));

    /* Allocate DMA buffers */
    s_ctx.rx_buf = heap_caps_malloc(SPI_BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_ctx.tx_buf = heap_caps_malloc(SPI_BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_ctx.rx_buf || !s_ctx.tx_buf) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffers");
        return ESP_ERR_NO_MEM;
    }
    memset(s_ctx.rx_buf, 0, SPI_BUF_SIZE);
    memset(s_ctx.tx_buf, 0xFF, SPI_BUF_SIZE);

    ESP_LOGI(TAG, "SPI flash emulator initialized");
    return ESP_OK;
}

esp_err_t spi_flash_emu_start(chip_type_t chip)
{
    const spi_chip_info_t *info = spi_chip_find(chip);
    if (!info) {
        ESP_LOGE(TAG, "Unknown SPI chip type: %d", chip);
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ctx.running) {
        spi_flash_emu_stop();
    }

    s_ctx.chip_info = info;
    s_ctx.write_enable = false;
    s_ctx.four_byte_mode = info->four_byte_addr;
    s_ctx.status_reg1 = 0;
    s_ctx.status_reg2 = 0;
    s_ctx.status_reg3 = 0;
    s_ctx.powered_down = false;

    /* Configure SPI slave */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .sclk_io_num = PIN_SPI_CLK,
        .quadwp_io_num = PIN_SPI_WP,
        .quadhd_io_num = PIN_SPI_HD,
        .max_transfer_sz = SPI_BUF_SIZE,
    };

    spi_slave_interface_config_t slave_cfg = {
        .mode = 0,                  /* SPI Mode 0 (CPOL=0, CPHA=0) */
        .spics_io_num = PIN_SPI_CS,
        .queue_size = 1,
        .flags = 0,
        .post_setup_cb = spi_post_setup_cb,
        .post_trans_cb = spi_post_trans_cb,
    };

    /* Enable pull-ups on SPI pins */
    gpio_set_pull_mode(PIN_SPI_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_SPI_CLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_SPI_CS, GPIO_PULLUP_ONLY);

    esp_err_t err = spi_slave_initialize(SPI2_HOST, &bus_cfg, &slave_cfg, SPI_DMA_CHAN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI slave init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Start the processing task on Core 1 for lowest latency */
    s_ctx.running = true;
    xTaskCreatePinnedToCore(spi_emu_task, "spi_emu", 8192, &s_ctx, 24,
                            &s_ctx.task, 1);

    ESP_LOGI(TAG, "Started emulating %s (size=%" PRIu32 ", JEDEC=%02X %02X %02X, %s addr)",
             info->name, info->total_size,
             info->jedec_manufacturer, info->jedec_memory_type, info->jedec_capacity,
             info->four_byte_addr ? "4-byte" : "3-byte");
    return ESP_OK;
}

esp_err_t spi_flash_emu_stop(void)
{
    if (!s_ctx.running) return ESP_OK;

    s_ctx.running = false;
    if (s_ctx.task) {
        vTaskDelay(pdMS_TO_TICKS(200));
        s_ctx.task = NULL;
    }

    spi_slave_free(SPI2_HOST);
    ESP_LOGI(TAG, "SPI emulation stopped");
    return ESP_OK;
}

esp_err_t spi_flash_emu_set_chip(chip_type_t chip)
{
    spi_flash_emu_stop();
    return spi_flash_emu_start(chip);
}

const emu_stats_t *spi_flash_emu_get_stats(void)
{
    return &g_spi_stats;
}

void spi_flash_emu_reset_stats(void)
{
    memset(&g_spi_stats, 0, sizeof(g_spi_stats));
}
