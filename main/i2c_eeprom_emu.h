#pragma once

#include "romemu_common.h"
#include "esp_err.h"

/**
 * Initialize I2C EEPROM emulator.
 * Sets up the I2C slave peripheral.
 */
esp_err_t i2c_eeprom_emu_init(void);

/**
 * Start emulation with the specified chip type.
 * The active ROM slot must already be inserted.
 */
esp_err_t i2c_eeprom_emu_start(chip_type_t chip);

/**
 * Stop emulation (release I2C peripheral).
 */
esp_err_t i2c_eeprom_emu_stop(void);

/**
 * Reconfigure for a new chip type (stops + restarts).
 */
esp_err_t i2c_eeprom_emu_set_chip(chip_type_t chip);

/**
 * Get current emulator statistics.
 */
const emu_stats_t *i2c_eeprom_emu_get_stats(void);

/**
 * Reset statistics counters.
 */
void i2c_eeprom_emu_reset_stats(void);
