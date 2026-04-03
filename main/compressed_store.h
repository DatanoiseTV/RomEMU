#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Block-compressed ROM storage with LRU decompression cache.
 *
 * Images are stored as LZ4-compressed 4KB blocks in PSRAM.
 * A small LRU cache in internal SRAM provides fast repeated reads.
 *
 * Typical compression ratios for firmware images:
 *   - Erased regions (0xFF): ~250:1
 *   - Code sections: ~1.5:1 to 2:1
 *   - Overall 16MB coreboot image: ~3:1 to 5:1
 */

#define CSTORE_BLOCK_SIZE    4096    /* 4 KB per block (flash sector size) */
#define CSTORE_CACHE_LINES   8       /* 8 cached decompressed blocks */
#define CSTORE_MAX_BLOCKS    16384   /* Up to 64 MB / 4 KB */

/* Per-block metadata */
typedef struct {
    uint32_t    offset;         /* Byte offset into compressed data buffer */
    uint16_t    comp_size;      /* Compressed size (0 = erased block, all 0xFF) */
    uint16_t    _reserved;
} cstore_block_info_t;

/* Cache line */
typedef struct {
    uint32_t    block_idx;      /* Which block is cached here */
    uint32_t    access_tick;    /* For LRU eviction */
    bool        valid;
    bool        dirty;          /* Modified but not yet re-compressed */
    uint8_t     data[CSTORE_BLOCK_SIZE];
} cstore_cache_line_t;

/* Compressed store instance */
typedef struct cstore_t {
    /* Block index */
    cstore_block_info_t *blocks;     /* Array of block metadata */
    uint32_t    num_blocks;          /* Total number of blocks */
    uint32_t    chip_size;           /* Virtual chip size in bytes */

    /* Compressed data in PSRAM */
    uint8_t    *comp_data;           /* PSRAM buffer for compressed blocks */
    uint32_t    comp_data_size;      /* Allocated size */
    uint32_t    comp_data_used;      /* Used bytes in compressed buffer */

    /* Decompression cache (internal SRAM for speed) */
    cstore_cache_line_t cache[CSTORE_CACHE_LINES];
    uint32_t    tick;                /* Monotonic counter for LRU */

    /* Stats */
    uint32_t    cache_hits;
    uint32_t    cache_misses;
    uint32_t    decompressions;
    uint32_t    compressions;
    uint32_t    raw_size;            /* Original uncompressed size */
    uint32_t    stored_size;         /* Actual compressed size in PSRAM */
} cstore_t;

/**
 * Create a compressed store from raw image data.
 * Compresses the image into PSRAM using LZ4 block compression.
 * chip_size is the emulated chip capacity (may be > image_size).
 *
 * Returns NULL on failure (out of memory).
 */
cstore_t *cstore_create(const uint8_t *raw_data, uint32_t image_size,
                        uint32_t chip_size);

/**
 * Free a compressed store and all its buffers.
 */
void cstore_destroy(cstore_t *cs);

/**
 * Read bytes from the compressed store.
 * Handles block boundaries transparently.
 * Addresses beyond allocated data return 0xFF.
 */
void cstore_read(cstore_t *cs, uint32_t addr, uint8_t *dst, uint32_t len);

/**
 * Read a single byte. Fast path for byte-level access.
 */
uint8_t cstore_read_byte(cstore_t *cs, uint32_t addr);

/**
 * Write bytes to the compressed store.
 * Decompresses affected blocks, modifies, marks dirty.
 * Flash semantics: can only clear bits (AND with existing data).
 */
void cstore_write(cstore_t *cs, uint32_t addr, const uint8_t *src, uint32_t len,
                  bool flash_semantics);

/**
 * Erase a range (set to 0xFF).
 * Affected blocks are marked as erased (zero storage).
 */
void cstore_erase(cstore_t *cs, uint32_t addr, uint32_t len);

/**
 * Flush all dirty cache lines back to compressed storage.
 */
void cstore_flush(cstore_t *cs);

/**
 * Get compression statistics as JSON.
 */
int cstore_get_stats_json(const cstore_t *cs, char *buf, size_t buf_size);

/**
 * Get the PSRAM memory used by this store (compressed data + metadata).
 */
uint32_t cstore_get_psram_usage(const cstore_t *cs);
