#include "web_server.h"
#include "sse_manager.h"
#include "rom_store.h"
#include "spi_flash_emu.h"
#include "i2c_eeprom_emu.h"
#include "wifi_manager.h"
#include "eth_manager.h"
#include "access_log.h"
#include "spi_flash_commands.h"
#include "romemu_common.h"
#include "gpio_control.h"
#include "compressed_store.h"
#include "embedded_files.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/socket.h>
#include "miniz.h"

static const char *TAG = "web_hdlr";

/* Forward declarations for slot action handlers */
esp_err_t handler_api_slot_upload(httpd_req_t *req);
esp_err_t handler_api_slot_insert(httpd_req_t *req);
esp_err_t handler_api_slot_eject(httpd_req_t *req);
esp_err_t handler_api_slot_label(httpd_req_t *req);

/* ---- Static file handlers (embedded gzip content) ---- */

esp_err_t handler_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)index_html_gz, index_html_gz_len);
}

esp_err_t handler_style(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)style_css_gz, style_css_gz_len);
}

esp_err_t handler_script(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)app_js_gz, app_js_gz_len);
}

esp_err_t handler_htmx(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)htmx_min_js_gz, htmx_min_js_gz_len);
}

/* ---- API: System status ---- */

esp_err_t handler_api_status(httpd_req_t *req)
{
    char buf[512];
    int active_spi = rom_store_get_active_idx(BUS_SPI);
    int active_i2c = rom_store_get_active_idx(BUS_I2C);

    const char *spi_chip_name = "none";
    const char *i2c_chip_name = "none";
    if (active_spi >= 0) {
        rom_slot_t *s = rom_store_get_slot(active_spi);
        if (s) {
            const spi_chip_info_t *ci = spi_chip_find(s->chip_type);
            if (ci) spi_chip_name = ci->name;
        }
    }
    if (active_i2c >= 0) {
        rom_slot_t *s = rom_store_get_slot(active_i2c);
        if (s) {
            const i2c_chip_info_t *ci = i2c_chip_find(s->chip_type);
            if (ci) i2c_chip_name = ci->name;
        }
    }

    int n = snprintf(buf, sizeof(buf),
        "{\"uptime\":%" PRIu64 ","
        "\"heap_free\":%u,\"psram_free\":%u,"
        "\"wifi_ip\":\"%s\",\"wifi_rssi\":%d,\"wifi_ap_mode\":%s,"
        "\"eth_ip\":\"%s\",\"eth_connected\":%s,"
        "\"target\":\"%s\","
        "\"spi_chip\":\"%s\",\"spi_slot\":%d,"
        "\"i2c_chip\":\"%s\",\"i2c_slot\":%d,"
        "\"sse_clients\":%d}",
        esp_timer_get_time() / 1000000ULL,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        wifi_manager_get_ip(), wifi_manager_get_rssi(),
        wifi_manager_is_ap_mode() ? "true" : "false",
        eth_manager_get_ip(),
        eth_manager_is_connected() ? "true" : "false",
#if CONFIG_IDF_TARGET_ESP32P4
        "ESP32-P4",
#else
        "ESP32-S3",
#endif
        spi_chip_name, active_spi,
        i2c_chip_name, active_i2c,
        sse_manager_client_count());

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

/* ---- API: ROM Slots ---- */

static void slot_to_json(cJSON *arr, int idx)
{
    rom_slot_t *s = rom_store_get_slot(idx);
    if (!s) return;

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "index", idx);
    cJSON_AddBoolToObject(obj, "occupied", s->occupied);
    cJSON_AddStringToObject(obj, "label", s->label);
    cJSON_AddBoolToObject(obj, "inserted", s->inserted);
    cJSON_AddNumberToObject(obj, "image_size", s->image_size);
    cJSON_AddNumberToObject(obj, "alloc_size", s->alloc_size);
    cJSON_AddBoolToObject(obj, "has_data", s->data != NULL || s->cstore != NULL);
    cJSON_AddBoolToObject(obj, "compressed", s->compressed);

    char crc_str[16];
    snprintf(crc_str, sizeof(crc_str), "%08" PRIX32, s->checksum);
    cJSON_AddStringToObject(obj, "checksum", crc_str);

    const char *chip_name = "none";
    const char *bus_name = "none";
    uint32_t chip_size = 0;
    if (s->bus_type == BUS_SPI) {
        const spi_chip_info_t *ci = spi_chip_find(s->chip_type);
        if (ci) { chip_name = ci->name; chip_size = ci->total_size; }
        bus_name = "SPI";
    } else if (s->bus_type == BUS_I2C) {
        const i2c_chip_info_t *ci = i2c_chip_find(s->chip_type);
        if (ci) { chip_name = ci->name; chip_size = ci->total_size; }
        bus_name = "I2C";
    }
    cJSON_AddStringToObject(obj, "chip_name", chip_name);
    cJSON_AddStringToObject(obj, "bus", bus_name);
    cJSON_AddNumberToObject(obj, "chip_size", chip_size);
    cJSON_AddNumberToObject(obj, "chip_type", s->chip_type);

    cJSON_AddItemToArray(arr, obj);
}

esp_err_t handler_api_slots(httpd_req_t *req)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < ROM_SLOT_MAX; i++) {
        slot_to_json(arr, i);
    }
    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json, strlen(json));
    free(json);
    return ret;
}

/* ---- Slot action router ---- */

static int parse_slot_from_uri(const char *uri)
{
    /* URI format: /api/slots/N/action or /api/slots/N */
    const char *p = uri + strlen("/api/slots/");
    if (!p || !*p) return -1;
    int slot = atoi(p);
    if (slot < 0 || slot >= ROM_SLOT_MAX) return -1;
    return slot;
}

static const char *parse_action_from_uri(const char *uri)
{
    /* Find second '/' after /api/slots/N */
    const char *p = uri + strlen("/api/slots/");
    while (*p && *p != '/') p++;
    if (*p == '/') return p + 1;
    return NULL;
}

/* Compare action string ignoring query params (e.g. "upload?chip=4" matches "upload") */
static bool action_matches(const char *action, const char *name)
{
    if (!action) return false;
    size_t len = strlen(name);
    return (strncmp(action, name, len) == 0 &&
            (action[len] == '\0' || action[len] == '?'));
}

esp_err_t handler_api_slot_action(httpd_req_t *req)
{
    int slot = parse_slot_from_uri(req->uri);
    if (slot < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slot");
        return ESP_FAIL;
    }

    const char *action = parse_action_from_uri(req->uri);
    if (!action) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing action");
        return ESP_FAIL;
    }

    if (action_matches(action, "upload")) {
        return handler_api_slot_upload(req);
    } else if (action_matches(action, "insert")) {
        return handler_api_slot_insert(req);
    } else if (action_matches(action, "eject")) {
        return handler_api_slot_eject(req);
    } else if (action_matches(action, "label")) {
        return handler_api_slot_label(req);
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unknown action");
    return ESP_FAIL;
}

/* ---- Upload handler (multipart/form-data) ---- */

esp_err_t handler_api_slot_upload(httpd_req_t *req)
{
    int slot = parse_slot_from_uri(req->uri);
    if (slot < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slot");
        return ESP_FAIL;
    }

    rom_slot_t *s = rom_store_get_slot(slot);
    if (s && s->inserted) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Eject slot first");
        return ESP_FAIL;
    }

    int total = req->content_len;
    if (total <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Upload to slot %d: %d bytes", slot, total);

    /* Increase socket recv timeout for large uploads (default is too short) */
    int sockfd = httpd_req_to_sockfd(req);
    struct timeval tv = { .tv_sec = 30, .tv_usec = 0 };
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Allocate temporary buffer in PSRAM */
    uint8_t *buf = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    /* Receive the data */
    int received = 0;
    int timeout_count = 0;
    int last_log_pct = -1;
    while (received < total) {
        int ret = httpd_req_recv(req, (char *)buf + received, total - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                timeout_count++;
                if (timeout_count > 10) {
                    ESP_LOGE(TAG, "Upload stalled at %d/%d bytes", received, total);
                    heap_caps_free(buf);
                    httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Upload timeout");
                    return ESP_FAIL;
                }
                continue;
            }
            ESP_LOGE(TAG, "Upload recv error: %d at %d/%d bytes", ret, received, total);
            heap_caps_free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_FAIL;
        }
        timeout_count = 0;
        received += ret;

        /* Log progress every 10% */
        int pct = (int)((int64_t)received * 100 / total);
        if (pct / 10 > last_log_pct / 10) {
            ESP_LOGI(TAG, "Upload: %d%% (%d/%d bytes)", pct, received, total);
            last_log_pct = pct;
        }
    }

    ESP_LOGI(TAG, "Upload received: %d bytes", received);

    /* Parse chip_type and label from query string */
    char query[128] = {};
    char chip_str[16] = {};
    char label[32] = {};
    char orig_size_str[16] = {};
    chip_type_t chip_type = CHIP_W25Q128; /* default */

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "chip", chip_str, sizeof(chip_str)) == ESP_OK) {
            chip_type = (chip_type_t)atoi(chip_str);
        }
        httpd_query_key_value(query, "label", label, sizeof(label));
        httpd_query_key_value(query, "original_size", orig_size_str, sizeof(orig_size_str));
    }

    /* Check if data is compressed (client-side DEFLATE) */
    char compressed_hdr[16] = {};
    httpd_req_get_hdr_value_str(req, "X-Compressed", compressed_hdr, sizeof(compressed_hdr));

    uint8_t *final_buf = buf;
    int final_size = received;

    if (strcmp(compressed_hdr, "deflate") == 0) {
        /* Decompress raw DEFLATE data using miniz (in ESP32 ROM) */
        uint32_t original_size = 0;
        char orig_hdr[16] = {};
        httpd_req_get_hdr_value_str(req, "X-Original-Size", orig_hdr, sizeof(orig_hdr));
        if (orig_hdr[0]) {
            original_size = (uint32_t)atol(orig_hdr);
        } else if (orig_size_str[0]) {
            original_size = (uint32_t)atol(orig_size_str);
        }

        if (original_size == 0 || original_size > 64 * 1024 * 1024) {
            ESP_LOGE(TAG, "Invalid original size: %" PRIu32, original_size);
            heap_caps_free(buf);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing X-Original-Size header");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Decompressing: %d bytes -> %" PRIu32 " bytes", received, original_size);

        uint8_t *decompressed = heap_caps_malloc(original_size, MALLOC_CAP_SPIRAM);
        if (!decompressed) {
            ESP_LOGE(TAG, "Failed to allocate %" PRIu32 " bytes for decompression", original_size);
            heap_caps_free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
            return ESP_FAIL;
        }

        /* Use miniz tinfl for raw DEFLATE decompression */
        size_t decomp_size = tinfl_decompress_mem_to_mem(
            decompressed, original_size, buf, received,
            TINFL_FLAG_PARSE_ZLIB_HEADER  /* 0 for raw deflate */
        );

        /* If raw deflate failed, try without flags (pure raw) */
        if (decomp_size == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
            decomp_size = tinfl_decompress_mem_to_mem(
                decompressed, original_size, buf, received, 0);
        }

        if (decomp_size == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
            ESP_LOGE(TAG, "Decompression failed");
            heap_caps_free(decompressed);
            heap_caps_free(buf);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Decompression failed");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Decompressed: %u bytes (%.1f:1 ratio)",
                 (unsigned)decomp_size, (float)decomp_size / received);

        heap_caps_free(buf);
        final_buf = decompressed;
        final_size = (int)decomp_size;
    }

    esp_err_t err = rom_store_upload(slot, final_buf, final_size, chip_type,
                                      label[0] ? label : NULL);
    heap_caps_free(final_buf);

    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    char resp[128];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"slot\":%d,\"size\":%d}", slot, received);
    return httpd_resp_send(req, resp, strlen(resp));
}

/* ---- Download handler ---- */

esp_err_t handler_api_slot_download(httpd_req_t *req)
{
    /* Check if this is /api/slots/N/download */
    const char *action = parse_action_from_uri(req->uri);
    if (!action_matches(action, "download")) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }

    int slot = parse_slot_from_uri(req->uri);
    if (slot < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slot");
        return ESP_FAIL;
    }

    rom_slot_t *s = rom_store_get_slot(slot);
    if (!s || !s->occupied || (!s->data && !s->cstore)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Slot empty");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/octet-stream");
    char disposition[64];
    snprintf(disposition, sizeof(disposition),
             "attachment; filename=\"slot%d.bin\"", slot);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);

    /* Send in chunks - decompress on the fly if compressed */
    uint32_t sent = 0;
    uint32_t size = s->image_size;
    uint8_t chunk_buf[4096];
    while (sent < size) {
        uint32_t chunk = (size - sent > 4096) ? 4096 : (size - sent);
        if (s->compressed && s->cstore) {
            cstore_read(s->cstore, sent, chunk_buf, chunk);
            esp_err_t err = httpd_resp_send_chunk(req, (const char *)chunk_buf, chunk);
            if (err != ESP_OK) return err;
        } else if (s->data) {
            esp_err_t err = httpd_resp_send_chunk(req, (const char *)s->data + sent, chunk);
            if (err != ESP_OK) return err;
        }
        sent += chunk;
    }
    return httpd_resp_send_chunk(req, NULL, 0); /* End chunked response */
}

/* ---- Insert / Eject ---- */

esp_err_t handler_api_slot_insert(httpd_req_t *req)
{
    int slot = parse_slot_from_uri(req->uri);
    if (slot < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slot");
        return ESP_FAIL;
    }

    rom_slot_t *s = rom_store_get_slot(slot);
    if (!s || !s->occupied || (!s->data && !s->cstore)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Slot empty or no data");
        return ESP_FAIL;
    }

    esp_err_t err = rom_store_insert(slot);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Insert failed");
        return ESP_FAIL;
    }

    /* Start the appropriate emulator */
    if (s->bus_type == BUS_SPI) {
        spi_flash_emu_set_chip(s->chip_type);
    } else if (s->bus_type == BUS_I2C) {
        i2c_eeprom_emu_set_chip(s->chip_type);
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t handler_api_slot_eject(httpd_req_t *req)
{
    int slot = parse_slot_from_uri(req->uri);
    if (slot < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slot");
        return ESP_FAIL;
    }

    rom_slot_t *s = rom_store_get_slot(slot);
    if (s && s->inserted) {
        if (s->bus_type == BUS_SPI) {
            spi_flash_emu_stop();
        } else if (s->bus_type == BUS_I2C) {
            i2c_eeprom_emu_stop();
        }
    }

    rom_store_eject(slot);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* ---- Delete ---- */

esp_err_t handler_api_slot_delete(httpd_req_t *req)
{
    int slot = parse_slot_from_uri(req->uri);
    if (slot < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slot");
        return ESP_FAIL;
    }

    rom_slot_t *s = rom_store_get_slot(slot);
    if (s && s->inserted) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Eject slot first");
        return ESP_FAIL;
    }

    rom_store_delete(slot);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* ---- Label ---- */

esp_err_t handler_api_slot_label(httpd_req_t *req)
{
    int slot = parse_slot_from_uri(req->uri);
    if (slot < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slot");
        return ESP_FAIL;
    }

    char body[64] = {};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *label = cJSON_GetObjectItem(json, "label");
    if (label && cJSON_IsString(label)) {
        rom_store_set_label(slot, label->valuestring);
    }
    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* ---- Chip configuration ---- */

esp_err_t handler_api_chip_get(httpd_req_t *req)
{
    cJSON *obj = cJSON_CreateObject();

    /* SPI chips */
    cJSON *spi_arr = cJSON_AddArrayToObject(obj, "spi_chips");
    for (int i = 0; i < (int)SPI_CHIP_DB_COUNT; i++) {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddNumberToObject(c, "type", spi_chip_db[i].type);
        cJSON_AddStringToObject(c, "name", spi_chip_db[i].name);
        cJSON_AddNumberToObject(c, "size", spi_chip_db[i].total_size);
        char jedec[16];
        snprintf(jedec, sizeof(jedec), "%02X %02X %02X",
                 spi_chip_db[i].jedec_manufacturer,
                 spi_chip_db[i].jedec_memory_type,
                 spi_chip_db[i].jedec_capacity);
        cJSON_AddStringToObject(c, "jedec", jedec);
        cJSON_AddBoolToObject(c, "four_byte", spi_chip_db[i].four_byte_addr);
        cJSON_AddItemToArray(spi_arr, c);
    }

    /* I2C chips */
    cJSON *i2c_arr = cJSON_AddArrayToObject(obj, "i2c_chips");
    for (int i = 0; i < (int)I2C_CHIP_DB_COUNT; i++) {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddNumberToObject(c, "type", i2c_chip_db[i].type);
        cJSON_AddStringToObject(c, "name", i2c_chip_db[i].name);
        cJSON_AddNumberToObject(c, "size", i2c_chip_db[i].total_size);
        char addr_str[8];
        snprintf(addr_str, sizeof(addr_str), "0x%02X", i2c_chip_db[i].i2c_base_addr);
        cJSON_AddStringToObject(c, "addr", addr_str);
        cJSON_AddItemToArray(i2c_arr, c);
    }

    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json, strlen(json));
    free(json);
    return ret;
}

esp_err_t handler_api_chip_set(httpd_req_t *req)
{
    char body[128] = {};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *type_item = cJSON_GetObjectItem(json, "type");
    if (!type_item || !cJSON_IsNumber(type_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'type'");
        return ESP_FAIL;
    }

    chip_type_t chip = (chip_type_t)type_item->valueint;
    cJSON_Delete(json);

    bus_type_t bus = chip_get_bus(chip);
    if (bus == BUS_SPI) {
        spi_flash_emu_set_chip(chip);
    } else if (bus == BUS_I2C) {
        i2c_eeprom_emu_set_chip(chip);
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* ---- Statistics ---- */

esp_err_t handler_api_stats(httpd_req_t *req)
{
    const emu_stats_t *spi = spi_flash_emu_get_stats();
    const emu_stats_t *i2c = i2c_eeprom_emu_get_stats();

    char buf[512];
    int n = snprintf(buf, sizeof(buf),
        "{\"spi\":{\"reads\":%" PRIu64 ",\"writes\":%" PRIu64 ",\"erases\":%" PRIu64 ","
        "\"bytes_read\":%" PRIu64 ",\"bytes_written\":%" PRIu64 "},"
        "\"i2c\":{\"reads\":%" PRIu64 ",\"writes\":%" PRIu64 ",\"erases\":%" PRIu64 ","
        "\"bytes_read\":%" PRIu64 ",\"bytes_written\":%" PRIu64 "},"
        "\"log_overflow\":%" PRIu32 "}",
        spi->total_reads, spi->total_writes, spi->total_erases,
        spi->bytes_read, spi->bytes_written,
        i2c->total_reads, i2c->total_writes, i2c->total_erases,
        i2c->bytes_read, i2c->bytes_written,
        access_log_overflow_count());

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

esp_err_t handler_api_stats_reset(httpd_req_t *req)
{
    spi_flash_emu_reset_stats();
    i2c_eeprom_emu_reset_stats();
    access_log_clear();

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* ---- WiFi ---- */

esp_err_t handler_api_wifi_get(httpd_req_t *req)
{
    char buf[128];
    wifi_manager_get_info_json(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, strlen(buf));
}

esp_err_t handler_api_wifi_set(httpd_req_t *req)
{
    char body[128] = {};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    cJSON *pass = cJSON_GetObjectItem(json, "password");

    if (!ssid || !cJSON_IsString(ssid)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'ssid'");
        return ESP_FAIL;
    }

    const char *password = (pass && cJSON_IsString(pass)) ? pass->valuestring : "";
    esp_err_t err = wifi_manager_set_sta(ssid->valuestring, password);
    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        return httpd_resp_sendstr(req, "{\"ok\":true}");
    }
    return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Connection failed\"}");
}

/* ---- Log (JSON snapshot) ---- */

esp_err_t handler_api_log(httpd_req_t *req)
{
    /* Return last N entries as JSON array */
    static const char *op_names[] = { "read", "write", "erase", "cmd" };
    static const char *bus_names[] = { "none", "spi", "i2c" };

    access_log_entry_t entry;
    cJSON *arr = cJSON_CreateArray();
    int count = 0;

    while (count < 100 && access_log_pop(&entry)) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "ts", entry.timestamp_ms);

        char addr_str[12];
        snprintf(addr_str, sizeof(addr_str), "0x%06" PRIX32, entry.address);
        cJSON_AddStringToObject(obj, "addr", addr_str);

        cJSON_AddNumberToObject(obj, "len", entry.length);
        cJSON_AddStringToObject(obj, "op",
            entry.operation < 4 ? op_names[entry.operation] : "?");
        cJSON_AddStringToObject(obj, "bus",
            entry.bus < 3 ? bus_names[entry.bus] : "?");

        char cmd_str[6];
        snprintf(cmd_str, sizeof(cmd_str), "0x%02X", entry.command);
        cJSON_AddStringToObject(obj, "cmd", cmd_str);

        cJSON_AddItemToArray(arr, obj);
        count++;
    }

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json, strlen(json));
    free(json);
    return ret;
}

/* ---- SSE events endpoint ---- */

esp_err_t handler_api_events(httpd_req_t *req)
{
    return sse_manager_add_client(req);
}

/* ---- GPIO Control ---- */

esp_err_t handler_api_gpio_get(httpd_req_t *req)
{
    char buf[128];
    gpio_control_get_json(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, strlen(buf));
}

esp_err_t handler_api_gpio_action(httpd_req_t *req)
{
    const char *uri = req->uri;
    const char *action = uri + strlen("/api/gpio/");

    /* Optional JSON body for parameters */
    char body[128] = {};
    int body_len = httpd_req_recv(req, body, sizeof(body) - 1);
    uint32_t duration = 100;
    uint32_t param2 = 100;

    if (body_len > 0) {
        cJSON *json = cJSON_Parse(body);
        if (json) {
            cJSON *dur = cJSON_GetObjectItem(json, "duration");
            if (dur && cJSON_IsNumber(dur)) duration = dur->valueint;
            cJSON *p2 = cJSON_GetObjectItem(json, "settle");
            if (!p2) p2 = cJSON_GetObjectItem(json, "post_delay");
            if (p2 && cJSON_IsNumber(p2)) param2 = p2->valueint;
            cJSON_Delete(json);
        }
    }

    if (strcmp(action, "reset/assert") == 0) {
        gpio_control_reset_assert();
    } else if (strcmp(action, "reset/release") == 0) {
        gpio_control_reset_release();
    } else if (strcmp(action, "reset/pulse") == 0) {
        gpio_control_reset_pulse(duration);
    } else if (strcmp(action, "reset/sequence") == 0) {
        gpio_control_reset_sequence(duration, param2);
    } else if (strcmp(action, "power/on") == 0) {
        gpio_control_power_on();
    } else if (strcmp(action, "power/off") == 0) {
        gpio_control_power_off();
    } else if (strcmp(action, "power/cycle") == 0) {
        gpio_control_power_cycle(duration, param2);
    } else {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unknown GPIO action");
        return ESP_FAIL;
    }

    char resp[128];
    gpio_control_get_json(resp, sizeof(resp));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, strlen(resp));
}
