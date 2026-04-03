#pragma once

#include "romemu_common.h"

/**
 * Initialize the access log ring buffer.
 */
void access_log_init(void);

/**
 * Push a log entry from ISR context. Lock-free, single-producer safe.
 */
void access_log_push(const access_log_entry_t *entry);

/**
 * Push a log entry (convenience wrapper for non-ISR context).
 */
void access_log_push_event(bus_type_t bus, access_op_t op, uint8_t cmd,
                           uint32_t addr, uint16_t len, uint8_t slot);

/**
 * Read next available entry. Returns true if an entry was available.
 * Called from consumer task only.
 */
bool access_log_pop(access_log_entry_t *out);

/**
 * Get number of entries available to read.
 */
uint32_t access_log_available(void);

/**
 * Get overflow count (entries lost because buffer was full).
 */
uint32_t access_log_overflow_count(void);

/**
 * Clear the log buffer.
 */
void access_log_clear(void);
