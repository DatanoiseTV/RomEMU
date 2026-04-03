/**
 * Minimal LZ4 block compressor/decompressor.
 *
 * LZ4 block format:
 *   Each sequence consists of:
 *   - Token byte: high nibble = literal length, low nibble = match length - 4
 *   - If literal length == 15, additional bytes follow (add 255 each until < 255)
 *   - Literal bytes (0..N)
 *   - Offset: 2 bytes little-endian (distance back into decoded buffer)
 *   - If match length nibble == 15, additional bytes follow
 *
 * The last sequence has no match (only literals for the trailing bytes).
 */

#include "lz4_block.h"
#include <string.h>

/* ---- Compressor (used at upload time, not speed-critical) ---- */

#define HASH_LOG    12
#define HASH_SIZE   (1 << HASH_LOG)
#define MAX_DISTANCE 65535
#define MIN_MATCH    4
#define LAST_LITERALS 5

static inline uint32_t hash4(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return (v * 2654435761U) >> (32 - HASH_LOG);
}

static inline void write_le16(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}

static inline uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

size_t lz4_compress(const uint8_t *src, size_t src_size,
                    uint8_t *dst, size_t dst_capacity)
{
    if (src_size == 0) return 0;
    if (src_size > 0x7E000000) return 0; /* Too large */

    uint16_t hash_table[HASH_SIZE];
    memset(hash_table, 0, sizeof(hash_table));

    const uint8_t *ip = src;
    const uint8_t *anchor = src;
    const uint8_t *src_end = src + src_size;
    const uint8_t *match_limit = src_end - LAST_LITERALS;
    const uint8_t *mflimit = src_end - MIN_MATCH;

    uint8_t *op = dst;
    uint8_t *dst_end = dst + dst_capacity;

    if (src_size < MIN_MATCH + LAST_LITERALS + 1) goto _last_literals;

    ip++; /* First byte is never a match start */

    while (ip < mflimit) {
        uint32_t h = hash4(ip);
        const uint8_t *ref = src + hash_table[h];
        hash_table[h] = (uint16_t)(ip - src);

        /* Check match */
        if (ref < src || ip - ref > MAX_DISTANCE) { ip++; continue; }
        uint32_t v1, v2;
        memcpy(&v1, ip, 4);
        memcpy(&v2, ref, 4);
        if (v1 != v2) { ip++; continue; }

        /* Found a match - count match length */
        size_t lit_len = ip - anchor;
        const uint8_t *match_start = ip;
        ip += MIN_MATCH;
        const uint8_t *ref2 = ref + MIN_MATCH;
        while (ip < match_limit && *ip == *ref2) { ip++; ref2++; }
        size_t match_len = ip - match_start;
        uint16_t offset = (uint16_t)(match_start - ref);

        /* Encode token */
        if (op + 1 + lit_len + 2 + (lit_len / 255) + (match_len / 255) + 2 > dst_end)
            return 0; /* Output overflow */

        uint8_t *token = op++;
        /* Encode literal length */
        if (lit_len >= 15) {
            *token = 0xF0;
            size_t remaining = lit_len - 15;
            while (remaining >= 255) { *op++ = 255; remaining -= 255; }
            *op++ = (uint8_t)remaining;
        } else {
            *token = (uint8_t)(lit_len << 4);
        }

        /* Copy literals */
        memcpy(op, anchor, lit_len);
        op += lit_len;

        /* Encode offset */
        write_le16(op, offset);
        op += 2;

        /* Encode match length */
        size_t ml = match_len - MIN_MATCH;
        if (ml >= 15) {
            *token |= 0x0F;
            ml -= 15;
            while (ml >= 255) { *op++ = 255; ml -= 255; }
            *op++ = (uint8_t)ml;
        } else {
            *token |= (uint8_t)ml;
        }

        anchor = ip;

        /* Update hash for positions in the match */
        if (ip < mflimit) {
            hash_table[hash4(ip - 2)] = (uint16_t)(ip - 2 - src);
        }
    }

_last_literals:;
    /* Last literal sequence (no match) */
    size_t last_run = src_end - anchor;
    if (op + 1 + last_run + (last_run / 255) + 1 > dst_end) return 0;

    uint8_t *token = op++;
    if (last_run >= 15) {
        *token = 0xF0;
        size_t remaining = last_run - 15;
        while (remaining >= 255) { *op++ = 255; remaining -= 255; }
        *op++ = (uint8_t)remaining;
    } else {
        *token = (uint8_t)(last_run << 4);
    }
    memcpy(op, anchor, last_run);
    op += last_run;

    return op - dst;
}

/* ---- Decompressor (hot path, speed-critical) ---- */

size_t lz4_decompress(const uint8_t *src, size_t src_size,
                      uint8_t *dst, size_t original_size)
{
    const uint8_t *ip = src;
    const uint8_t *ip_end = src + src_size;
    uint8_t *op = dst;
    uint8_t *op_end = dst + original_size;

    while (ip < ip_end) {
        /* Read token */
        uint8_t token = *ip++;

        /* Decode literal length */
        size_t lit_len = token >> 4;
        if (lit_len == 15) {
            uint8_t s;
            do {
                if (ip >= ip_end) return 0;
                s = *ip++;
                lit_len += s;
            } while (s == 255);
        }

        /* Copy literals */
        if (lit_len > 0) {
            if (ip + lit_len > ip_end || op + lit_len > op_end) return 0;
            memcpy(op, ip, lit_len);
            ip += lit_len;
            op += lit_len;
        }

        /* Check if this is the last sequence (no match follows) */
        if (op == op_end) break;
        if (ip + 2 > ip_end) return 0;

        /* Decode offset */
        uint16_t offset = read_le16(ip);
        ip += 2;
        if (offset == 0) return 0;
        uint8_t *match = op - offset;
        if (match < dst) return 0;

        /* Decode match length */
        size_t match_len = (token & 0x0F) + MIN_MATCH;
        if ((token & 0x0F) == 15) {
            uint8_t s;
            do {
                if (ip >= ip_end) return 0;
                s = *ip++;
                match_len += s;
            } while (s == 255);
        }

        /* Copy match (may overlap) */
        if (op + match_len > op_end) return 0;
        if (offset >= 8 && match_len <= 32) {
            /* Fast path: no overlap possible, small copy */
            memcpy(op, match, match_len);
        } else {
            /* Byte-by-byte for overlapping matches */
            for (size_t i = 0; i < match_len; i++) {
                op[i] = match[i];
            }
        }
        op += match_len;
    }

    return op - dst;
}
