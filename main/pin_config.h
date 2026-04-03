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

/* ---- ESP32-P4-Nano ---- */

/* SPI Flash Emulation (SPI2 via GPIO matrix) */
#define PIN_SPI_CS      GPIO_NUM_6
#define PIN_SPI_MOSI    GPIO_NUM_7    /* IO0 in QSPI mode */
#define PIN_SPI_CLK     GPIO_NUM_8
#define PIN_SPI_MISO    GPIO_NUM_9    /* IO1 in QSPI mode */
#define PIN_SPI_WP      GPIO_NUM_10   /* IO2 in QSPI mode */
#define PIN_SPI_HD      GPIO_NUM_11   /* IO3 in QSPI mode */

/* I2C EEPROM Emulation */
#define PIN_I2C_SDA     GPIO_NUM_12
#define PIN_I2C_SCL     GPIO_NUM_13

/* Target Reset Control (active-low, open-drain) */
#define PIN_RESET_OUT   GPIO_NUM_14

/* Target Power Control (active-high, drives N-MOSFET gate) */
#define PIN_POWER_OUT   GPIO_NUM_15

/* Ethernet RMII pins (directly managed by ETH driver, listed for reference)
 * These are used by the IP101 PHY on the P4-Nano board.
 * TXD0=GPIO34, TXD1=GPIO35, TX_EN=GPIO33
 * RXD0=GPIO29, RXD1=GPIO30, CRS_DV=GPIO28
 * REF_CLK=GPIO50, MDC=GPIO31, MDIO=GPIO27
 * RST=GPIO32
 */
#define PIN_ETH_PHY_RST   GPIO_NUM_32
#define PIN_ETH_PHY_MDC   GPIO_NUM_31
#define PIN_ETH_PHY_MDIO  GPIO_NUM_27

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
