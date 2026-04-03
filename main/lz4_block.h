#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * Minimal LZ4 block compressor/decompressor.
 * Implements the LZ4 block format (not frame format).
 * Optimized for fast decompression on ESP32-S3.
 */

/**
 * Compress src into dst.
 * Returns compressed size, or 0 on failure.
 * dst must be at least lz4_compress_bound(src_size) bytes.
 */
size_t lz4_compress(const uint8_t *src, size_t src_size,
                    uint8_t *dst, size_t dst_capacity);

/**
 * Decompress src into dst.
 * original_size must be the exact original uncompressed size.
 * Returns number of bytes written to dst, or 0 on error.
 */
size_t lz4_decompress(const uint8_t *src, size_t src_size,
                      uint8_t *dst, size_t original_size);

/**
 * Worst-case compressed size for a given input size.
 */
static inline size_t lz4_compress_bound(size_t input_size)
{
    return input_size + (input_size / 255) + 16;
}
