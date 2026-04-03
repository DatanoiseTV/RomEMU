#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * GPIO control for target reset and power management.
 *
 * RESET pin: Active-low output. Directly drives the target's reset line.
 *            Can pulse (assert then release after delay) or hold.
 *
 * POWER pin: Active-high output. Drives a MOSFET gate to control target power.
 *            High = power on, Low = power off.
 */

/**
 * Initialize GPIO control outputs.
 */
esp_err_t gpio_control_init(void);

/**
 * Assert reset (pull low). Target is held in reset until released.
 */
void gpio_control_reset_assert(void);

/**
 * Release reset (let float / go high via pull-up).
 */
void gpio_control_reset_release(void);

/**
 * Pulse reset: assert for duration_ms, then release.
 * Blocking call.
 */
void gpio_control_reset_pulse(uint32_t duration_ms);

/**
 * Get current reset pin state. true = asserted (target in reset).
 */
bool gpio_control_reset_is_asserted(void);

/**
 * Power on: drive power MOSFET gate high.
 */
void gpio_control_power_on(void);

/**
 * Power off: drive power MOSFET gate low.
 */
void gpio_control_power_off(void);

/**
 * Get current power state. true = powered on.
 */
bool gpio_control_power_is_on(void);

/**
 * Full target reset sequence:
 * 1. Assert reset
 * 2. Wait pre_delay_ms
 * 3. (Caller does insert/eject in between)
 * 4. Release reset
 * 5. Wait post_delay_ms
 */
void gpio_control_reset_sequence(uint32_t pre_delay_ms, uint32_t post_delay_ms);

/**
 * Full power cycle sequence:
 * 1. Power off
 * 2. Wait off_time_ms
 * 3. Power on
 * 4. Wait on_settle_ms
 */
void gpio_control_power_cycle(uint32_t off_time_ms, uint32_t on_settle_ms);

/**
 * Get GPIO control state as JSON into buf.
 */
int gpio_control_get_json(char *buf, size_t buf_size);
