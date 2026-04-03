#include "compressed_store.h"
#include "lz4_block.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "cstore";

/* Check if a block is entirely 0xFF */
static bool is_erased_block(const uint8_t *data, size_t size)
{
    /* Check 4 bytes at a time for speed */
    const uint32_t *p32 = (const uint32_t *)data;
    size_t words = size / 4;
    for (size_t i = 0; i < words; i++) {
        if (p32[i] != 0xFFFFFFFF) return false;
    }
    for (size_t i = words * 4; i < size; i++) {
        if (data[i] != 0xFF) return false;
    }
    return true;
}

cstore_t *cstore_create(const uint8_t *raw_data, uint32_t image_size,
                        uint32_t chip_size)
{
    if (chip_size == 0) chip_size = image_size;
    uint32_t num_blocks = (chip_size + CSTORE_BLOCK_SIZE - 1) / CSTORE_BLOCK_SIZE;
    if (num_blocks > CSTORE_MAX_BLOCKS) {
        ESP_LOGE(TAG, "Too many blocks: %u (max %d)", num_blocks, CSTORE_MAX_BLOCKS);
        return NULL;
    }

    /* Allocate the store struct in internal RAM (contains the cache) */
    cstore_t *cs = heap_caps_calloc(1, sizeof(cstore_t), MALLOC_CAP_INTERNAL);
    if (!cs) {
        ESP_LOGE(TAG, "Failed to allocate cstore_t (%u bytes)", (unsigned)sizeof(cstore_t));
        return NULL;
    }

    cs->num_blocks = num_blocks;
    cs->chip_size = chip_size;
    cs->raw_size = image_size;

    /* Allocate block index in PSRAM */
    cs->blocks = heap_caps_calloc(num_blocks, sizeof(cstore_block_info_t), MALLOC_CAP_SPIRAM);
    if (!cs->blocks) {
        ESP_LOGE(TAG, "Failed to allocate block index (%u entries)", num_blocks);
        cstore_destroy(cs);
        return NULL;
    }

    /* First pass: compress all blocks to calculate total compressed size.
     * Use a temporary buffer for each block's compression. */
    uint8_t *temp_comp = heap_caps_malloc(lz4_compress_bound(CSTORE_BLOCK_SIZE),
                                          MALLOC_CAP_INTERNAL);
    if (!temp_comp) {
        ESP_LOGE(TAG, "Failed to allocate temp compression buffer");
        cstore_destroy(cs);
        return NULL;
    }

    /* Calculate total compressed size */
    uint32_t total_comp = 0;
    uint32_t erased_blocks = 0;
    uint32_t incompressible = 0;

    for (uint32_t i = 0; i < num_blocks; i++) {
        uint32_t block_offset = i * CSTORE_BLOCK_SIZE;
        uint32_t block_len = CSTORE_BLOCK_SIZE;
        if (block_offset + block_len > chip_size) {
            block_len = chip_size - block_offset;
        }

        /* Blocks beyond image data are erased (0xFF) */
        if (block_offset >= image_size) {
            cs->blocks[i].comp_size = 0; /* erased */
            erased_blocks++;
            continue;
        }

        /* Prepare block data (may be partial at end) */
        uint8_t block_buf[CSTORE_BLOCK_SIZE];
        memset(block_buf, 0xFF, CSTORE_BLOCK_SIZE);
        uint32_t copy_len = (block_offset + block_len <= image_size) ?
                             block_len : (image_size - block_offset);
        memcpy(block_buf, raw_data + block_offset, copy_len);

        /* Check if erased */
        if (is_erased_block(block_buf, block_len)) {
            cs->blocks[i].comp_size = 0;
            erased_blocks++;
            continue;
        }

        /* Compress */
        size_t comp_size = lz4_compress(block_buf, block_len, temp_comp,
                                        lz4_compress_bound(CSTORE_BLOCK_SIZE));
        if (comp_size == 0 || comp_size >= block_len) {
            /* Incompressible - store raw */
            cs->blocks[i].comp_size = block_len;
            total_comp += block_len;
            incompressible++;
        } else {
            cs->blocks[i].comp_size = (uint16_t)comp_size;
            total_comp += comp_size;
        }
    }

    /* Allocate compressed data buffer in PSRAM */
    cs->comp_data_size = total_comp;
    if (total_comp > 0) {
        cs->comp_data = heap_caps_malloc(total_comp, MALLOC_CAP_SPIRAM);
        if (!cs->comp_data) {
            ESP_LOGE(TAG, "Failed to allocate %u bytes for compressed data", total_comp);
            heap_caps_free(temp_comp);
            cstore_destroy(cs);
            return NULL;
        }
    }

    /* Second pass: actually compress and store */
    uint32_t write_pos = 0;
    for (uint32_t i = 0; i < num_blocks; i++) {
        if (cs->blocks[i].comp_size == 0) continue; /* erased */

        uint32_t block_offset = i * CSTORE_BLOCK_SIZE;
        uint32_t block_len = CSTORE_BLOCK_SIZE;
        if (block_offset + block_len > chip_size) {
            block_len = chip_size - block_offset;
        }

        uint8_t block_buf[CSTORE_BLOCK_SIZE];
        memset(block_buf, 0xFF, CSTORE_BLOCK_SIZE);
        uint32_t copy_len = (block_offset + block_len <= image_size) ?
                             block_len : (image_size - block_offset);
        memcpy(block_buf, raw_data + block_offset, copy_len);

        cs->blocks[i].offset = write_pos;

        if (cs->blocks[i].comp_size == block_len) {
            /* Store raw (incompressible) */
            memcpy(cs->comp_data + write_pos, block_buf, block_len);
            write_pos += block_len;
        } else {
            /* Store compressed */
            size_t comp_size = lz4_compress(block_buf, block_len, temp_comp,
                                            lz4_compress_bound(CSTORE_BLOCK_SIZE));
            memcpy(cs->comp_data + write_pos, temp_comp, comp_size);
            write_pos += comp_size;
        }
    }

    cs->comp_data_used = write_pos;
    cs->stored_size = write_pos;

    heap_caps_free(temp_comp);

    /* Initialize cache */
    for (int i = 0; i < CSTORE_CACHE_LINES; i++) {
        cs->cache[i].valid = false;
        cs->cache[i].dirty = false;
        cs->cache[i].block_idx = UINT32_MAX;
    }
    cs->tick = 0;

    float ratio = (image_size > 0) ? (float)image_size / (write_pos > 0 ? write_pos : 1) : 0;
    ESP_LOGI(TAG, "Compressed %u -> %u bytes (%.1f:1 ratio, %u erased blocks, %u raw blocks)",
             image_size, write_pos, ratio, erased_blocks, incompressible);
    ESP_LOGI(TAG, "  %u blocks, %u KB PSRAM used (index: %u KB, data: %u KB)",
             num_blocks,
             (unsigned)(cstore_get_psram_usage(cs) / 1024),
             (unsigned)(num_blocks * sizeof(cstore_block_info_t) / 1024),
             (unsigned)(write_pos / 1024));

    return cs;
}

void cstore_destroy(cstore_t *cs)
{
    if (!cs) return;

    if (cs->comp_data) heap_caps_free(cs->comp_data);
    if (cs->blocks) heap_caps_free(cs->blocks);
    heap_caps_free(cs);
}

/* ---- Cache management ---- */

static cstore_cache_line_t *cache_find(cstore_t *cs, uint32_t block_idx)
{
    for (int i = 0; i < CSTORE_CACHE_LINES; i++) {
        if (cs->cache[i].valid && cs->cache[i].block_idx == block_idx) {
            cs->cache[i].access_tick = ++cs->tick;
            cs->cache_hits++;
            return &cs->cache[i];
        }
    }
    return NULL;
}

static void flush_cache_line(cstore_t *cs, cstore_cache_line_t *line);

static cstore_cache_line_t *cache_evict_lru(cstore_t *cs)
{
    /* Find LRU (or first invalid) cache line */
    int best = 0;
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < CSTORE_CACHE_LINES; i++) {
        if (!cs->cache[i].valid) return &cs->cache[i];
        if (cs->cache[i].access_tick < oldest) {
            oldest = cs->cache[i].access_tick;
            best = i;
        }
    }

    cstore_cache_line_t *victim = &cs->cache[best];
    /* Flush if dirty */
    if (victim->dirty) {
        flush_cache_line(cs, victim);
    }
    victim->valid = false;
    return victim;
}

static cstore_cache_line_t *cache_load_block(cstore_t *cs, uint32_t block_idx)
{
    cstore_cache_line_t *line = cache_find(cs, block_idx);
    if (line) return line;

    cs->cache_misses++;
    line = cache_evict_lru(cs);
    line->block_idx = block_idx;
    line->access_tick = ++cs->tick;
    line->dirty = false;

    if (block_idx >= cs->num_blocks) {
        /* Beyond chip - all 0xFF */
        memset(line->data, 0xFF, CSTORE_BLOCK_SIZE);
        line->valid = true;
        return line;
    }

    cstore_block_info_t *bi = &cs->blocks[block_idx];

    if (bi->comp_size == 0) {
        /* Erased block */
        memset(line->data, 0xFF, CSTORE_BLOCK_SIZE);
    } else {
        uint32_t block_len = CSTORE_BLOCK_SIZE;
        uint32_t block_offset = block_idx * CSTORE_BLOCK_SIZE;
        if (block_offset + block_len > cs->chip_size) {
            block_len = cs->chip_size - block_offset;
        }

        if (bi->comp_size == block_len) {
            /* Stored raw (incompressible) */
            memset(line->data, 0xFF, CSTORE_BLOCK_SIZE);
            memcpy(line->data, cs->comp_data + bi->offset, block_len);
        } else {
            /* Decompress */
            memset(line->data, 0xFF, CSTORE_BLOCK_SIZE);
            size_t dec = lz4_decompress(cs->comp_data + bi->offset, bi->comp_size,
                                        line->data, block_len);
            if (dec != block_len) {
                ESP_LOGE(TAG, "Decompression failed for block %u (got %u, expected %u)",
                         block_idx, (unsigned)dec, block_len);
                memset(line->data, 0xFF, CSTORE_BLOCK_SIZE);
            }
            cs->decompressions++;
        }
    }

    line->valid = true;
    return line;
}

/* Flush a dirty cache line: re-compress and write back */
static void flush_cache_line(cstore_t *cs, cstore_cache_line_t *line)
{
    if (!line->dirty || !line->valid) return;

    uint32_t block_idx = line->block_idx;
    uint32_t block_len = CSTORE_BLOCK_SIZE;
    uint32_t block_offset = block_idx * CSTORE_BLOCK_SIZE;
    if (block_offset + block_len > cs->chip_size) {
        block_len = cs->chip_size - block_offset;
    }

    cstore_block_info_t *bi = &cs->blocks[block_idx];

    /* Check if now erased */
    if (is_erased_block(line->data, block_len)) {
        bi->comp_size = 0;
        line->dirty = false;
        return;
    }

    /* Re-compress into temporary buffer */
    uint8_t temp[CSTORE_BLOCK_SIZE + 256]; /* LZ4 worst case for 4K */
    size_t comp_size = lz4_compress(line->data, block_len, temp, sizeof(temp));

    if (comp_size == 0 || comp_size >= block_len) {
        /* Store raw */
        comp_size = block_len;
        memcpy(temp, line->data, block_len);
    }

    /*
     * Simple strategy: append to compressed buffer.
     * Old compressed data becomes dead space. For a development tool this is
     * acceptable. A compaction pass could reclaim space if needed.
     */
    if (cs->comp_data_used + comp_size <= cs->comp_data_size) {
        bi->offset = cs->comp_data_used;
        bi->comp_size = (uint16_t)comp_size;
        memcpy(cs->comp_data + cs->comp_data_used, temp, comp_size);
        cs->comp_data_used += comp_size;
        cs->compressions++;
    } else {
        ESP_LOGW(TAG, "Compressed buffer full, cannot flush block %u", block_idx);
        /* Keep the block cached and dirty - reads still work from cache */
    }

    line->dirty = false;
}

/* ---- Public read/write API ---- */

void cstore_read(cstore_t *cs, uint32_t addr, uint8_t *dst, uint32_t len)
{
    while (len > 0) {
        if (addr >= cs->chip_size) {
            /* Beyond chip size - return 0xFF */
            memset(dst, 0xFF, len);
            return;
        }

        uint32_t block_idx = addr / CSTORE_BLOCK_SIZE;
        uint32_t block_off = addr % CSTORE_BLOCK_SIZE;
        uint32_t avail = CSTORE_BLOCK_SIZE - block_off;
        uint32_t chunk = (len < avail) ? len : avail;

        cstore_cache_line_t *line = cache_load_block(cs, block_idx);
        memcpy(dst, line->data + block_off, chunk);

        addr += chunk;
        dst += chunk;
        len -= chunk;
    }
}

uint8_t cstore_read_byte(cstore_t *cs, uint32_t addr)
{
    if (addr >= cs->chip_size) return 0xFF;

    uint32_t block_idx = addr / CSTORE_BLOCK_SIZE;
    uint32_t block_off = addr % CSTORE_BLOCK_SIZE;

    cstore_cache_line_t *line = cache_load_block(cs, block_idx);
    return line->data[block_off];
}

void cstore_write(cstore_t *cs, uint32_t addr, const uint8_t *src, uint32_t len,
                  bool flash_semantics)
{
    while (len > 0) {
        if (addr >= cs->chip_size) return;

        uint32_t block_idx = addr / CSTORE_BLOCK_SIZE;
        uint32_t block_off = addr % CSTORE_BLOCK_SIZE;
        uint32_t avail = CSTORE_BLOCK_SIZE - block_off;
        uint32_t chunk = (len < avail) ? len : avail;

        cstore_cache_line_t *line = cache_load_block(cs, block_idx);

        if (flash_semantics) {
            /* Flash write: can only clear bits (AND) */
            for (uint32_t i = 0; i < chunk; i++) {
                line->data[block_off + i] &= src[i];
            }
        } else {
            memcpy(line->data + block_off, src, chunk);
        }
        line->dirty = true;

        addr += chunk;
        src += chunk;
        len -= chunk;
    }
}

void cstore_erase(cstore_t *cs, uint32_t addr, uint32_t len)
{
    /* Align to block boundaries for efficiency */
    while (len > 0) {
        if (addr >= cs->chip_size) return;

        uint32_t block_idx = addr / CSTORE_BLOCK_SIZE;
        uint32_t block_off = addr % CSTORE_BLOCK_SIZE;

        if (block_off == 0 && len >= CSTORE_BLOCK_SIZE) {
            /* Erase entire block - mark as erased, no need to decompress */
            cs->blocks[block_idx].comp_size = 0;

            /* Invalidate cache line if cached */
            for (int i = 0; i < CSTORE_CACHE_LINES; i++) {
                if (cs->cache[i].valid && cs->cache[i].block_idx == block_idx) {
                    memset(cs->cache[i].data, 0xFF, CSTORE_BLOCK_SIZE);
                    cs->cache[i].dirty = false;
                    break;
                }
            }

            addr += CSTORE_BLOCK_SIZE;
            len -= CSTORE_BLOCK_SIZE;
        } else {
            /* Partial block erase - load, modify, mark dirty */
            uint32_t avail = CSTORE_BLOCK_SIZE - block_off;
            uint32_t chunk = (len < avail) ? len : avail;

            cstore_cache_line_t *line = cache_load_block(cs, block_idx);
            memset(line->data + block_off, 0xFF, chunk);
            line->dirty = true;

            addr += chunk;
            len -= chunk;
        }
    }
}

void cstore_flush(cstore_t *cs)
{
    for (int i = 0; i < CSTORE_CACHE_LINES; i++) {
        if (cs->cache[i].valid && cs->cache[i].dirty) {
            flush_cache_line(cs, &cs->cache[i]);
        }
    }
}

int cstore_get_stats_json(const cstore_t *cs, char *buf, size_t buf_size)
{
    if (!cs) {
        return snprintf(buf, buf_size, "{\"enabled\":false}");
    }
    float ratio = (cs->stored_size > 0) ?
                  (float)cs->raw_size / cs->stored_size : 0;
    return snprintf(buf, buf_size,
        "{\"enabled\":true,\"raw_size\":%u,\"compressed_size\":%u,"
        "\"ratio\":%.1f,\"num_blocks\":%u,"
        "\"psram_used\":%u,\"cache_hits\":%u,\"cache_misses\":%u,"
        "\"decompressions\":%u,\"compressions\":%u}",
        cs->raw_size, cs->stored_size, ratio, cs->num_blocks,
        cstore_get_psram_usage(cs),
        cs->cache_hits, cs->cache_misses,
        cs->decompressions, cs->compressions);
}

uint32_t cstore_get_psram_usage(const cstore_t *cs)
{
    if (!cs) return 0;
    return cs->comp_data_used +
           (cs->num_blocks * sizeof(cstore_block_info_t));
}
