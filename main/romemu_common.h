#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define ROM_SLOT_MAX        1
#define ACCESS_LOG_ENTRIES  8192
#define MAX_SSE_CLIENTS     4

/* ---- Chip type enumeration ---- */
typedef enum {
    CHIP_NONE = 0,
    /* SPI Flash - Winbond */
    CHIP_W25Q16,        /*   2 MB */
    CHIP_W25Q32,        /*   4 MB */
    CHIP_W25Q64,        /*   8 MB */
    CHIP_W25Q128,       /*  16 MB */
    CHIP_W25Q256,       /*  32 MB - 4-byte address mode */
    CHIP_W25Q512,       /*  64 MB - 4-byte address mode */
    /* SPI Flash - Macronix */
    CHIP_MX25L1606E,    /*   2 MB */
    CHIP_MX25L3233F,    /*   4 MB */
    CHIP_MX25L6433F,    /*   8 MB */
    CHIP_MX25L12835F,   /*  16 MB */
    CHIP_MX25L25645G,   /*  32 MB - 4-byte address mode */
    /* SPI Flash - ISSI */
    CHIP_IS25LP064,     /*   8 MB */
    CHIP_IS25LP128,     /*  16 MB */
    /* SPI Flash - Microchip SST */
    CHIP_SST26VF032B,   /*   4 MB */
    CHIP_SST26VF064B,   /*   8 MB */
    /* I2C EEPROMs */
    CHIP_24C02,         /*  256 B */
    CHIP_24C04,         /*  512 B */
    CHIP_24C08,         /*   1 KB */
    CHIP_24C16,         /*   2 KB */
    CHIP_24C32,         /*   4 KB */
    CHIP_24C64,         /*   8 KB */
    CHIP_24C128,        /*  16 KB */
    CHIP_24C256,        /*  32 KB */
    CHIP_24C512,        /*  64 KB */
    CHIP_24C1024,       /* 128 KB */
    CHIP_TYPE_COUNT
} chip_type_t;

typedef enum {
    BUS_NONE = 0,
    BUS_SPI,
    BUS_I2C,
} bus_type_t;

/* ---- SPI Flash chip info ---- */
typedef struct {
    chip_type_t type;
    uint8_t     jedec_manufacturer;
    uint8_t     jedec_memory_type;
    uint8_t     jedec_capacity;
    uint32_t    total_size;
    uint16_t    page_size;
    uint16_t    sector_size;        /* 4 KB typical */
    uint32_t    block_size;         /* 64 KB typical */
    bool        four_byte_addr;     /* W25Q256/512 */
    const char *name;
} spi_chip_info_t;

/* ---- I2C EEPROM chip info ---- */
typedef struct {
    chip_type_t type;
    uint8_t     i2c_base_addr;      /* 0x50 */
    uint32_t    total_size;
    uint16_t    page_size;
    uint8_t     addr_bytes;         /* 1 or 2 */
    uint8_t     addr_bits_in_dev;   /* bits of address encoded in device addr (0,1,2,3) */
    const char *name;
} i2c_chip_info_t;

/* Forward declaration */
typedef struct cstore_t cstore_t;

/* ---- ROM slot ---- */
typedef struct {
    bool        occupied;
    char        label[32];
    chip_type_t chip_type;
    bus_type_t  bus_type;
    uint32_t    image_size;          /* actual uploaded data size */
    uint32_t    alloc_size;         /* PSRAM allocation size (compressed) */
    uint32_t    checksum;           /* CRC32 */
    uint8_t    *data;               /* PSRAM pointer (raw, for small images) */
    cstore_t   *cstore;             /* Compressed store (for large images) */
    bool        compressed;         /* true = using cstore, false = raw data ptr */
    bool        inserted;           /* actively being served to host */
} rom_slot_t;

/* ---- Access log entry (16 bytes, ISR-safe) ---- */
typedef enum {
    OP_READ  = 0,
    OP_WRITE = 1,
    OP_ERASE = 2,
    OP_CMD   = 3,
} access_op_t;

typedef struct {
    uint32_t    timestamp_ms;
    uint32_t    address;
    uint16_t    length;
    uint8_t     bus;                /* bus_type_t */
    uint8_t     operation;          /* access_op_t */
    uint8_t     command;            /* SPI opcode or I2C sub-op */
    uint8_t     slot;
    uint8_t     _pad[2];
} __attribute__((packed)) access_log_entry_t;

_Static_assert(sizeof(access_log_entry_t) == 16, "log entry must be 16 bytes");

/* ---- Access log ring buffer ---- */
typedef struct {
    access_log_entry_t entries[ACCESS_LOG_ENTRIES];
    volatile uint32_t  write_idx;
    volatile uint32_t  read_idx;
    uint32_t           overflow_count;
} access_log_t;

/* ---- Statistics ---- */
typedef struct {
    uint64_t total_reads;
    uint64_t total_writes;
    uint64_t total_erases;
    uint64_t bytes_read;
    uint64_t bytes_written;
} emu_stats_t;
