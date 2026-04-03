#pragma once

#include "romemu_common.h"

/* ---- SPI Flash opcodes ---- */
#define SPI_CMD_WRITE_ENABLE        0x06
#define SPI_CMD_WRITE_DISABLE       0x04
#define SPI_CMD_READ_STATUS_1       0x05
#define SPI_CMD_READ_STATUS_2       0x35
#define SPI_CMD_READ_STATUS_3       0x15
#define SPI_CMD_WRITE_STATUS_1      0x01
#define SPI_CMD_WRITE_STATUS_2      0x31
#define SPI_CMD_WRITE_STATUS_3      0x11
#define SPI_CMD_READ_DATA           0x03
#define SPI_CMD_FAST_READ           0x0B
#define SPI_CMD_DUAL_READ           0x3B
#define SPI_CMD_QUAD_READ           0x6B
#define SPI_CMD_DUAL_IO_READ        0xBB
#define SPI_CMD_QUAD_IO_READ        0xEB
#define SPI_CMD_PAGE_PROGRAM        0x02
#define SPI_CMD_SECTOR_ERASE        0x20    /* 4 KB */
#define SPI_CMD_BLOCK_ERASE_32K     0x52
#define SPI_CMD_BLOCK_ERASE_64K     0xD8
#define SPI_CMD_CHIP_ERASE          0xC7
#define SPI_CMD_CHIP_ERASE_ALT      0x60
#define SPI_CMD_READ_JEDEC_ID       0x9F
#define SPI_CMD_READ_DEVICE_ID      0xAB
#define SPI_CMD_READ_UNIQUE_ID      0x4B
#define SPI_CMD_READ_SFDP           0x5A
#define SPI_CMD_ENABLE_RESET        0x66
#define SPI_CMD_RESET               0x99
#define SPI_CMD_POWER_DOWN          0xB9
#define SPI_CMD_RELEASE_PD          0xAB
/* 4-byte address commands (W25Q256/512) */
#define SPI_CMD_READ_DATA_4B        0x13
#define SPI_CMD_FAST_READ_4B        0x0C
#define SPI_CMD_PAGE_PROGRAM_4B     0x12
#define SPI_CMD_SECTOR_ERASE_4B     0x21
#define SPI_CMD_BLOCK_ERASE_64K_4B  0xDC
#define SPI_CMD_ENTER_4BYTE_MODE    0xB7
#define SPI_CMD_EXIT_4BYTE_MODE     0xE9

/* ---- Chip database ---- */
static const spi_chip_info_t spi_chip_db[] = {
    {
        .type = CHIP_W25Q16,
        .jedec_manufacturer = 0xEF, .jedec_memory_type = 0x40, .jedec_capacity = 0x15,
        .total_size = 2 * 1024 * 1024, .page_size = 256,
        .sector_size = 4096, .block_size = 65536,
        .four_byte_addr = false, .name = "W25Q16JV"
    },
    {
        .type = CHIP_W25Q32,
        .jedec_manufacturer = 0xEF, .jedec_memory_type = 0x40, .jedec_capacity = 0x16,
        .total_size = 4 * 1024 * 1024, .page_size = 256,
        .sector_size = 4096, .block_size = 65536,
        .four_byte_addr = false, .name = "W25Q32JV"
    },
    {
        .type = CHIP_W25Q64,
        .jedec_manufacturer = 0xEF, .jedec_memory_type = 0x40, .jedec_capacity = 0x17,
        .total_size = 8 * 1024 * 1024, .page_size = 256,
        .sector_size = 4096, .block_size = 65536,
        .four_byte_addr = false, .name = "W25Q64JV"
    },
    {
        .type = CHIP_W25Q128,
        .jedec_manufacturer = 0xEF, .jedec_memory_type = 0x40, .jedec_capacity = 0x18,
        .total_size = 16 * 1024 * 1024, .page_size = 256,
        .sector_size = 4096, .block_size = 65536,
        .four_byte_addr = false, .name = "W25Q128JV"
    },
    {
        .type = CHIP_W25Q256,
        .jedec_manufacturer = 0xEF, .jedec_memory_type = 0x40, .jedec_capacity = 0x19,
        .total_size = 32 * 1024 * 1024, .page_size = 256,
        .sector_size = 4096, .block_size = 65536,
        .four_byte_addr = true, .name = "W25Q256JV"
    },
    {
        .type = CHIP_W25Q512,
        .jedec_manufacturer = 0xEF, .jedec_memory_type = 0x40, .jedec_capacity = 0x20,
        .total_size = 64 * 1024 * 1024, .page_size = 256,
        .sector_size = 4096, .block_size = 65536,
        .four_byte_addr = true, .name = "W25Q512JV"
    },
    {
        .type = CHIP_MX25L1606E,
        .jedec_manufacturer = 0xC2, .jedec_memory_type = 0x20, .jedec_capacity = 0x15,
        .total_size = 2 * 1024 * 1024, .page_size = 256,
        .sector_size = 4096, .block_size = 65536,
        .four_byte_addr = false, .name = "MX25L1606E"
    },
    {
        .type = CHIP_MX25L3233F,
        .jedec_manufacturer = 0xC2, .jedec_memory_type = 0x20, .jedec_capacity = 0x16,
        .total_size = 4 * 1024 * 1024, .page_size = 256,
        .sector_size = 4096, .block_size = 65536,
        .four_byte_addr = false, .name = "MX25L3233F"
    },
    {
        .type = CHIP_MX25L6433F,
        .jedec_manufacturer = 0xC2, .jedec_memory_type = 0x20, .jedec_capacity = 0x17,
        .total_size = 8 * 1024 * 1024, .page_size = 256,
        .sector_size = 4096, .block_size = 65536,
        .four_byte_addr = false, .name = "MX25L6433F"
    },
    {
        .type = CHIP_MX25L12835F,
        .jedec_manufacturer = 0xC2, .jedec_memory_type = 0x20, .jedec_capacity = 0x18,
        .total_size = 16 * 1024 * 1024, .page_size = 256,
        .sector_size = 4096, .block_size = 65536,
        .four_byte_addr = false, .name = "MX25L12835F"
    },
    {
        .type = CHIP_MX25L25645G,
        .jedec_manufacturer = 0xC2, .jedec_memory_type = 0x20, .jedec_capacity = 0x19,
        .total_size = 32 * 1024 * 1024, .page_size = 256,
        .sector_size = 4096, .block_size = 65536,
        .four_byte_addr = true, .name = "MX25L25645G"
    },
    /* ISSI */
    {
        .type = CHIP_IS25LP064,
        .jedec_manufacturer = 0x9D, .jedec_memory_type = 0x60, .jedec_capacity = 0x17,
        .total_size = 8 * 1024 * 1024, .page_size = 256,
        .sector_size = 4096, .block_size = 65536,
        .four_byte_addr = false, .name = "IS25LP064"
    },
    {
        .type = CHIP_IS25LP128,
        .jedec_manufacturer = 0x9D, .jedec_memory_type = 0x60, .jedec_capacity = 0x18,
        .total_size = 16 * 1024 * 1024, .page_size = 256,
        .sector_size = 4096, .block_size = 65536,
        .four_byte_addr = false, .name = "IS25LP128"
    },
    /* Microchip SST */
    {
        .type = CHIP_SST26VF032B,
        .jedec_manufacturer = 0xBF, .jedec_memory_type = 0x26, .jedec_capacity = 0x42,
        .total_size = 4 * 1024 * 1024, .page_size = 256,
        .sector_size = 4096, .block_size = 65536,
        .four_byte_addr = false, .name = "SST26VF032B"
    },
    {
        .type = CHIP_SST26VF064B,
        .jedec_manufacturer = 0xBF, .jedec_memory_type = 0x26, .jedec_capacity = 0x43,
        .total_size = 8 * 1024 * 1024, .page_size = 256,
        .sector_size = 4096, .block_size = 65536,
        .four_byte_addr = false, .name = "SST26VF064B"
    },
};

#define SPI_CHIP_DB_COUNT (sizeof(spi_chip_db) / sizeof(spi_chip_db[0]))

static const i2c_chip_info_t i2c_chip_db[] = {
    { .type = CHIP_24C02,  .i2c_base_addr = 0x50, .total_size = 256,    .page_size = 8,   .addr_bytes = 1, .addr_bits_in_dev = 0, .name = "24C02" },
    { .type = CHIP_24C04,  .i2c_base_addr = 0x50, .total_size = 512,    .page_size = 16,  .addr_bytes = 1, .addr_bits_in_dev = 1, .name = "24C04" },
    { .type = CHIP_24C08,  .i2c_base_addr = 0x50, .total_size = 1024,   .page_size = 16,  .addr_bytes = 1, .addr_bits_in_dev = 2, .name = "24C08" },
    { .type = CHIP_24C16,  .i2c_base_addr = 0x50, .total_size = 2048,   .page_size = 16,  .addr_bytes = 1, .addr_bits_in_dev = 3, .name = "24C16" },
    { .type = CHIP_24C32,  .i2c_base_addr = 0x50, .total_size = 4096,   .page_size = 32,  .addr_bytes = 2, .addr_bits_in_dev = 0, .name = "24C32" },
    { .type = CHIP_24C64,  .i2c_base_addr = 0x50, .total_size = 8192,   .page_size = 32,  .addr_bytes = 2, .addr_bits_in_dev = 0, .name = "24C64" },
    { .type = CHIP_24C128, .i2c_base_addr = 0x50, .total_size = 16384,  .page_size = 64,  .addr_bytes = 2, .addr_bits_in_dev = 0, .name = "24C128" },
    { .type = CHIP_24C256, .i2c_base_addr = 0x50, .total_size = 32768,  .page_size = 64,  .addr_bytes = 2, .addr_bits_in_dev = 0, .name = "24C256" },
    { .type = CHIP_24C512,  .i2c_base_addr = 0x50, .total_size = 65536,  .page_size = 128, .addr_bytes = 2, .addr_bits_in_dev = 0, .name = "24C512" },
    { .type = CHIP_24C1024, .i2c_base_addr = 0x50, .total_size = 131072, .page_size = 256, .addr_bytes = 2, .addr_bits_in_dev = 1, .name = "24C1024" },
};

#define I2C_CHIP_DB_COUNT (sizeof(i2c_chip_db) / sizeof(i2c_chip_db[0]))

/* Lookup helpers */
static inline const spi_chip_info_t *spi_chip_find(chip_type_t type) {
    for (int i = 0; i < (int)SPI_CHIP_DB_COUNT; i++) {
        if (spi_chip_db[i].type == type) return &spi_chip_db[i];
    }
    return NULL;
}

static inline const i2c_chip_info_t *i2c_chip_find(chip_type_t type) {
    for (int i = 0; i < (int)I2C_CHIP_DB_COUNT; i++) {
        if (i2c_chip_db[i].type == type) return &i2c_chip_db[i];
    }
    return NULL;
}

static inline bus_type_t chip_get_bus(chip_type_t type) {
    if (type >= CHIP_W25Q16 && type <= CHIP_SST26VF064B) return BUS_SPI;
    if (type >= CHIP_24C02 && type <= CHIP_24C1024) return BUS_I2C;
    return BUS_NONE;
}
