#pragma once

#include "esp_http_server.h"

/**
 * Initialize SSE manager. Starts the background broadcast task.
 */
void sse_manager_init(void);

/**
 * Register a new SSE client connection.
 * Called from the /api/events HTTP handler.
 * Returns ESP_OK if the client was added.
 */
esp_err_t sse_manager_add_client(httpd_req_t *req);

/**
 * Get number of connected SSE clients.
 */
int sse_manager_client_count(void);

/**
 * Send a slot state change notification to all SSE clients.
 * Called after upload, insert, eject, delete.
 */
void sse_manager_notify_slot_change(void);
