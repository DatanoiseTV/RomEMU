#pragma once

#include "romemu_common.h"
#include "esp_err.h"

/**
 * Initialize SPI flash emulator.
 * Sets up the SPI slave peripheral on FSPI (SPI2).
 */
esp_err_t spi_flash_emu_init(void);

/**
 * Start emulation with the specified chip type.
 */
esp_err_t spi_flash_emu_start(chip_type_t chip);

/**
 * Stop emulation.
 */
esp_err_t spi_flash_emu_stop(void);

/**
 * Reconfigure for a new chip type.
 */
esp_err_t spi_flash_emu_set_chip(chip_type_t chip);

/**
 * Get current emulator statistics.
 */
const emu_stats_t *spi_flash_emu_get_stats(void);

/**
 * Reset statistics counters.
 */
void spi_flash_emu_reset_stats(void);
