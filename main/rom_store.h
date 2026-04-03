#pragma once

#include "romemu_common.h"
#include "esp_err.h"

/**
 * Initialize ROM store. Allocates PSRAM for slots, loads metadata from NVS.
 */
esp_err_t rom_store_init(void);

/**
 * Get pointer to a ROM slot (0-based index).
 */
rom_slot_t *rom_store_get_slot(int slot_idx);

/**
 * Upload image data to a slot. Copies data into PSRAM.
 * Slot must not be currently inserted.
 */
esp_err_t rom_store_upload(int slot_idx, const uint8_t *data, uint32_t size,
                           chip_type_t chip_type, const char *label);

/**
 * Delete image from a slot. Frees PSRAM, removes NVS metadata.
 */
esp_err_t rom_store_delete(int slot_idx);

/**
 * Insert slot - makes it the active ROM for its bus type.
 * Only one SPI slot and one I2C slot can be inserted at a time.
 */
esp_err_t rom_store_insert(int slot_idx);

/**
 * Eject slot - removes it from active emulation.
 */
esp_err_t rom_store_eject(int slot_idx);

/**
 * Set label for a slot.
 */
esp_err_t rom_store_set_label(int slot_idx, const char *label);

/**
 * Get the currently inserted slot for a bus type. Returns NULL if none.
 */
rom_slot_t *rom_store_get_active(bus_type_t bus);

/**
 * Get the slot index of the active slot for a bus type. Returns -1 if none.
 */
int rom_store_get_active_idx(bus_type_t bus);

/**
 * Persist current slot metadata to NVS.
 */
esp_err_t rom_store_save_metadata(void);

/**
 * Compute CRC32 of slot data.
 */
uint32_t rom_store_crc32(const uint8_t *data, uint32_t len);
