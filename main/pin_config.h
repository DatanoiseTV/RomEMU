#pragma once

#include "driver/gpio.h"
#include "sdkconfig.h"

/*
 * Pin assignments per target board.
 *
 * ESP32-S3: IOMUX pins for SPI2/FSPI, any GPIO for I2C/control
 * ESP32-P4:  GPIO-matrix routed SPI, I2C on available pins, RMII for Ethernet
 */

#if CONFIG_IDF_TARGET_ESP32P4

/* ---- ESP32-P4-Nano (Waveshare) ----
 *
 * Available header GPIOs (18 total):
 *   Left  header: 4, 5, 20, 21, 22, 23, 32, 33, 36
 *   Right header: 2, 3, 6, 45, 46, 47, 48, 53, 54
 *   Board I2C:    7 (SDA), 8 (SCL)
 *
 * Reserved (not usable): 0,1 (XTAL), 7,8 (I2C bus), 24-27 (USB),
 *   37,38 (UART0), C6 pins, RMII pins (28-31,34,35,50 — internal to board)
 *
 * Pin assignment strategy:
 *   SPI (6 pins)  → right header, grouped for clean wiring
 *   I2C (2 pins)  → board I2C bus (GPIO 7/8)
 *   Control       → left header
 */

/* SPI Flash Emulation — right header (SPI2 via GPIO matrix) */
#define PIN_SPI_CS      GPIO_NUM_45
#define PIN_SPI_CLK     GPIO_NUM_46
#define PIN_SPI_MOSI    GPIO_NUM_47   /* IO0 in QSPI mode */
#define PIN_SPI_MISO    GPIO_NUM_48   /* IO1 in QSPI mode */
#define PIN_SPI_WP      GPIO_NUM_53   /* IO2 in QSPI mode */
#define PIN_SPI_HD      GPIO_NUM_54   /* IO3 in QSPI mode */

/* I2C EEPROM Emulation — board I2C bus */
#define PIN_I2C_SDA     GPIO_NUM_7
#define PIN_I2C_SCL     GPIO_NUM_8

/* Target Reset Control — left header (active-low, open-drain) */
#define PIN_RESET_OUT   GPIO_NUM_4

/* Target Power Control — left header (active-high, drives N-MOSFET gate) */
#define PIN_POWER_OUT   GPIO_NUM_5

/* Spare GPIOs on left header: 20, 21, 22, 23, 32, 33, 36
 * Spare GPIOs on right header: 2, 3, 6
 */

/* Ethernet RMII pins (internal to P4-Nano board, directly managed by ETH driver)
 * TXD0=GPIO34, TXD1=GPIO35, TX_EN=GPIO49
 * RXD0=GPIO29, RXD1=GPIO30, CRS_DV=GPIO28
 * REF_CLK=GPIO50 (50MHz from 25MHz xtal via PHY freq doubling)
 * MDIO=GPIO52, MDC=GPIO31
 * PHY_RST=GPIO51
 */
#define PIN_ETH_PHY_RST   GPIO_NUM_51
#define PIN_ETH_PHY_MDC   GPIO_NUM_31
#define PIN_ETH_PHY_MDIO  GPIO_NUM_52

/* No onboard addressable LED on P4-Nano */
#define PIN_LED_STATUS  GPIO_NUM_NC

#elif CONFIG_IDF_TARGET_ESP32S3

/* ---- ESP32-S3-DevKitC ---- */

/* SPI Flash Emulation (SPI2/FSPI - IOMUX pins for minimum latency) */
#define PIN_SPI_CS      GPIO_NUM_10
#define PIN_SPI_MOSI    GPIO_NUM_11   /* IO0 in QSPI mode */
#define PIN_SPI_CLK     GPIO_NUM_12
#define PIN_SPI_MISO    GPIO_NUM_13   /* IO1 in QSPI mode */
#define PIN_SPI_WP      GPIO_NUM_14   /* IO2 in QSPI mode */
#define PIN_SPI_HD      GPIO_NUM_9    /* IO3 in QSPI mode */

/* I2C EEPROM Emulation */
#define PIN_I2C_SDA     GPIO_NUM_1
#define PIN_I2C_SCL     GPIO_NUM_2

/* Target Reset Control */
#define PIN_RESET_OUT   GPIO_NUM_4

/* Target Power Control */
#define PIN_POWER_OUT   GPIO_NUM_5

/* Status LED (onboard RGB on most ESP32-S3 DevKitC) */
#define PIN_LED_STATUS  GPIO_NUM_48

#else
#error "Unsupported target. Use esp32s3 or esp32p4."
#endif
