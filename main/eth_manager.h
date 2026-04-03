#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * Ethernet manager for ESP32-P4 with IP101 PHY (RMII).
 * On non-P4 targets, all functions are no-ops.
 */

/** Initialize Ethernet with IP101 PHY. */
esp_err_t eth_manager_init(void);

/** Get Ethernet IP as string. */
const char *eth_manager_get_ip(void);

/** Check if Ethernet link is up and has IP. */
bool eth_manager_is_connected(void);

/** Get info as JSON. */
int eth_manager_get_info_json(char *buf, size_t buf_size);
