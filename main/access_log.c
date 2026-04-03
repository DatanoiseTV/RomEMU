#include "access_log.h"
#include "esp_timer.h"
#include "esp_attr.h"

static access_log_t s_log;

void access_log_init(void)
{
    memset(&s_log, 0, sizeof(s_log));
}

void IRAM_ATTR access_log_push(const access_log_entry_t *entry)
{
    uint32_t next = (s_log.write_idx + 1) % ACCESS_LOG_ENTRIES;
    if (next == s_log.read_idx) {
        /* Buffer full - overwrite oldest */
        s_log.overflow_count++;
        s_log.read_idx = (s_log.read_idx + 1) % ACCESS_LOG_ENTRIES;
    }
    s_log.entries[s_log.write_idx] = *entry;
    s_log.write_idx = next;
}

void access_log_push_event(bus_type_t bus, access_op_t op, uint8_t cmd,
                           uint32_t addr, uint16_t len, uint8_t slot)
{
    access_log_entry_t entry = {
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .address = addr,
        .length = len,
        .bus = (uint8_t)bus,
        .operation = (uint8_t)op,
        .command = cmd,
        .slot = slot,
    };
    access_log_push(&entry);
}

bool access_log_pop(access_log_entry_t *out)
{
    if (s_log.read_idx == s_log.write_idx) return false;
    *out = s_log.entries[s_log.read_idx];
    s_log.read_idx = (s_log.read_idx + 1) % ACCESS_LOG_ENTRIES;
    return true;
}

uint32_t access_log_available(void)
{
    uint32_t w = s_log.write_idx;
    uint32_t r = s_log.read_idx;
    if (w >= r) return w - r;
    return ACCESS_LOG_ENTRIES - r + w;
}

uint32_t access_log_overflow_count(void)
{
    return s_log.overflow_count;
}

void access_log_clear(void)
{
    s_log.read_idx = s_log.write_idx;
    s_log.overflow_count = 0;
}
