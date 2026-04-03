#pragma once

#include "esp_http_server.h"
#include "esp_err.h"

/**
 * Start the HTTP server and register all routes.
 */
esp_err_t web_server_start(void);

/**
 * Stop the HTTP server.
 */
esp_err_t web_server_stop(void);

/**
 * Get the server handle (for SSE manager).
 */
httpd_handle_t web_server_get_handle(void);
