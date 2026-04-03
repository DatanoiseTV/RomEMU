#pragma once

#include "esp_err.h"

/**
 * Initialize WiFi subsystem.
 * Tries STA mode with stored credentials. Falls back to AP mode if STA fails.
 * Registers mDNS as "romemu.local".
 */
esp_err_t wifi_manager_init(void);

/**
 * Set STA credentials and restart WiFi in STA mode.
 */
esp_err_t wifi_manager_set_sta(const char *ssid, const char *password);

/**
 * Get current WiFi info as JSON string into buf.
 * Returns number of bytes written.
 */
int wifi_manager_get_info_json(char *buf, size_t buf_size);

/**
 * Get IP address string. Returns "0.0.0.0" if not connected.
 */
const char *wifi_manager_get_ip(void);

/**
 * Get WiFi RSSI. Returns 0 if in AP mode.
 */
int8_t wifi_manager_get_rssi(void);

/**
 * Returns true if operating in AP mode.
 */
bool wifi_manager_is_ap_mode(void);
