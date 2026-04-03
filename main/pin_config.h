#pragma once

#include "driver/gpio.h"

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

/* Target Reset Control (active-low, directly drives target RESET#) */
#define PIN_RESET_OUT   GPIO_NUM_4

/* Target Power Control (active-high, drives N-MOSFET gate) */
#define PIN_POWER_OUT   GPIO_NUM_5

/* Status LED (onboard RGB on most ESP32-S3 DevKitC) */
#define PIN_LED_STATUS  GPIO_NUM_48
