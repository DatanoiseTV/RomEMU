#include "rom_store.h"
#include "spi_flash_commands.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include <stdio.h>

static const char *TAG = "rom_store";

static rom_slot_t s_slots[ROM_SLOT_MAX];
static int s_active_spi_slot = -1;
static int s_active_i2c_slot = -1;

/* Simple CRC32 (no table, good enough for checksums) */
uint32_t rom_store_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

static void load_slot_metadata(int idx)
{
    nvs_handle_t nvs;
    char ns[16];
    snprintf(ns, sizeof(ns), "slot%d", idx);

    if (nvs_open(ns, NVS_READONLY, &nvs) != ESP_OK) return;

    rom_slot_t *s = &s_slots[idx];
    uint8_t chip = 0;
    size_t label_len = sizeof(s->label);

    if (nvs_get_u8(nvs, "chip", &chip) == ESP_OK) {
        s->chip_type = (chip_type_t)chip;
        s->bus_type = chip_get_bus(s->chip_type);
        nvs_get_u32(nvs, "size", &s->image_size);
        nvs_get_u32(nvs, "crc", &s->checksum);
        nvs_get_str(nvs, "label", s->label, &label_len);
        /* Data must be re-uploaded (PSRAM is volatile) */
        s->occupied = false;
        s->inserted = false;
        s->data = NULL;
        ESP_LOGI(TAG, "Slot %d metadata: %s (%s, %u bytes) - needs re-upload",
                 idx, s->label, s->chip_type ? "configured" : "none", s->image_size);
    }

    nvs_close(nvs);
}

static void save_slot_metadata(int idx)
{
    nvs_handle_t nvs;
    char ns[16];
    snprintf(ns, sizeof(ns), "slot%d", idx);

    if (nvs_open(ns, NVS_READWRITE, &nvs) != ESP_OK) return;

    rom_slot_t *s = &s_slots[idx];
    if (s->occupied) {
        nvs_set_u8(nvs, "chip", (uint8_t)s->chip_type);
        nvs_set_u32(nvs, "size", s->image_size);
        nvs_set_u32(nvs, "crc", s->checksum);
        nvs_set_str(nvs, "label", s->label);
    } else {
        nvs_erase_all(nvs);
    }
    nvs_commit(nvs);
    nvs_close(nvs);
}

esp_err_t rom_store_init(void)
{
    memset(s_slots, 0, sizeof(s_slots));
    s_active_spi_slot = -1;
    s_active_i2c_slot = -1;

    /* Load metadata from NVS for each slot */
    for (int i = 0; i < ROM_SLOT_MAX; i++) {
        load_slot_metadata(i);
    }

    ESP_LOGI(TAG, "ROM store initialized. PSRAM free: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return ESP_OK;
}

rom_slot_t *rom_store_get_slot(int slot_idx)
{
    if (slot_idx < 0 || slot_idx >= ROM_SLOT_MAX) return NULL;
    return &s_slots[slot_idx];
}

esp_err_t rom_store_upload(int slot_idx, const uint8_t *data, uint32_t size,
                           chip_type_t chip_type, const char *label)
{
    if (slot_idx < 0 || slot_idx >= ROM_SLOT_MAX) return ESP_ERR_INVALID_ARG;

    rom_slot_t *s = &s_slots[slot_idx];
    if (s->inserted) {
        ESP_LOGE(TAG, "Cannot upload to inserted slot %d", slot_idx);
        return ESP_ERR_INVALID_STATE;
    }

    /* Free existing data */
    if (s->data) {
        heap_caps_free(s->data);
        s->data = NULL;
    }

    /* Determine chip capacity from database */
    uint32_t chip_size = size;
    bus_type_t bus = chip_get_bus(chip_type);
    if (bus == BUS_SPI) {
        const spi_chip_info_t *info = spi_chip_find(chip_type);
        if (info) chip_size = info->total_size;
    } else if (bus == BUS_I2C) {
        const i2c_chip_info_t *info = i2c_chip_find(chip_type);
        if (info) chip_size = info->total_size;
    }

    /*
     * Dynamic PSRAM allocation:
     * - Try to allocate the full chip size first
     * - If chip is larger than available PSRAM, allocate as much as we can
     *   (at least the uploaded image size). Reads beyond allocated region
     *   return 0xFF (erased state), which is correct behavior.
     */
    uint32_t want_size = chip_size > size ? chip_size : size;
    uint32_t alloc_size = want_size;
    s->data = heap_caps_malloc(alloc_size, MALLOC_CAP_SPIRAM);

    if (!s->data && alloc_size > size) {
        /* Full chip doesn't fit — try allocating just enough for the image
         * plus some margin for writes, rounded up to 64K */
        alloc_size = (size + 0xFFFF) & ~0xFFFF;
        if (alloc_size < size) alloc_size = size;
        s->data = heap_caps_malloc(alloc_size, MALLOC_CAP_SPIRAM);
    }

    if (!s->data) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes in PSRAM for slot %d "
                 "(free: %u)", alloc_size, slot_idx,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return ESP_ERR_NO_MEM;
    }

    if (alloc_size < chip_size) {
        ESP_LOGW(TAG, "Slot %d: allocated %u of %u bytes (%.0f%%). "
                 "Reads beyond %u will return 0xFF.",
                 slot_idx, alloc_size, chip_size,
                 100.0f * alloc_size / chip_size, alloc_size);
    }

    /* Fill with 0xFF (erased state), then copy image */
    memset(s->data, 0xFF, alloc_size);
    memcpy(s->data, data, size);

    s->occupied = true;
    s->chip_type = chip_type;
    s->bus_type = bus;
    s->image_size = size;
    s->alloc_size = alloc_size;
    s->checksum = rom_store_crc32(data, size);
    s->inserted = false;
    if (label && label[0]) {
        strncpy(s->label, label, sizeof(s->label) - 1);
        s->label[sizeof(s->label) - 1] = '\0';
    }

    save_slot_metadata(slot_idx);

    ESP_LOGI(TAG, "Slot %d: uploaded %u bytes as %s (CRC32: %08X)",
             slot_idx, size, s->label[0] ? s->label : "(unnamed)", s->checksum);
    return ESP_OK;
}

esp_err_t rom_store_delete(int slot_idx)
{
    if (slot_idx < 0 || slot_idx >= ROM_SLOT_MAX) return ESP_ERR_INVALID_ARG;

    rom_slot_t *s = &s_slots[slot_idx];
    if (s->inserted) {
        ESP_LOGE(TAG, "Cannot delete inserted slot %d, eject first", slot_idx);
        return ESP_ERR_INVALID_STATE;
    }

    if (s->data) {
        heap_caps_free(s->data);
        s->data = NULL;
    }
    memset(s, 0, sizeof(rom_slot_t));
    save_slot_metadata(slot_idx);

    ESP_LOGI(TAG, "Slot %d deleted", slot_idx);
    return ESP_OK;
}

esp_err_t rom_store_insert(int slot_idx)
{
    if (slot_idx < 0 || slot_idx >= ROM_SLOT_MAX) return ESP_ERR_INVALID_ARG;

    rom_slot_t *s = &s_slots[slot_idx];
    if (!s->occupied || !s->data) {
        ESP_LOGE(TAG, "Slot %d is empty, cannot insert", slot_idx);
        return ESP_ERR_INVALID_STATE;
    }

    /* Eject any currently inserted slot on the same bus */
    int *active_idx = (s->bus_type == BUS_SPI) ? &s_active_spi_slot : &s_active_i2c_slot;
    if (*active_idx >= 0 && *active_idx != slot_idx) {
        s_slots[*active_idx].inserted = false;
        ESP_LOGI(TAG, "Auto-ejected slot %d", *active_idx);
    }

    s->inserted = true;
    *active_idx = slot_idx;

    ESP_LOGI(TAG, "Slot %d inserted on %s bus", slot_idx,
             s->bus_type == BUS_SPI ? "SPI" : "I2C");
    return ESP_OK;
}

esp_err_t rom_store_eject(int slot_idx)
{
    if (slot_idx < 0 || slot_idx >= ROM_SLOT_MAX) return ESP_ERR_INVALID_ARG;

    rom_slot_t *s = &s_slots[slot_idx];
    if (!s->inserted) return ESP_OK;

    s->inserted = false;

    int *active_idx = (s->bus_type == BUS_SPI) ? &s_active_spi_slot : &s_active_i2c_slot;
    if (*active_idx == slot_idx) {
        *active_idx = -1;
    }

    ESP_LOGI(TAG, "Slot %d ejected", slot_idx);
    return ESP_OK;
}

esp_err_t rom_store_set_label(int slot_idx, const char *label)
{
    if (slot_idx < 0 || slot_idx >= ROM_SLOT_MAX) return ESP_ERR_INVALID_ARG;
    rom_slot_t *s = &s_slots[slot_idx];
    strncpy(s->label, label, sizeof(s->label) - 1);
    s->label[sizeof(s->label) - 1] = '\0';
    save_slot_metadata(slot_idx);
    return ESP_OK;
}

rom_slot_t *rom_store_get_active(bus_type_t bus)
{
    int idx = (bus == BUS_SPI) ? s_active_spi_slot : s_active_i2c_slot;
    if (idx < 0) return NULL;
    rom_slot_t *s = &s_slots[idx];
    return (s->inserted && s->data) ? s : NULL;
}

int rom_store_get_active_idx(bus_type_t bus)
{
    return (bus == BUS_SPI) ? s_active_spi_slot : s_active_i2c_slot;
}

esp_err_t rom_store_save_metadata(void)
{
    for (int i = 0; i < ROM_SLOT_MAX; i++) {
        save_slot_metadata(i);
    }
    return ESP_OK;
}
