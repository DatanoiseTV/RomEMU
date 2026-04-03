# RomEMU

**SPI Flash & I2C EEPROM emulator with real-time web interface for ESP32-S3 and ESP32-P4.**

RomEMU turns an ESP32 dev board into a drop-in replacement for common SPI flash and I2C EEPROM chips. Upload firmware images over WiFi or Ethernet, insert them into a virtual socket, and the target system boots from them — no physical chip flashing required.

Built for fast iteration during firmware development, BIOS porting, coreboot bringup, and hardware reverse engineering.

### Supported Boards

| Board | MCU | PSRAM | Connectivity | Notes |
|-------|-----|-------|--------------|-------|
| ESP32-S3-DevKitC (N16R8) | ESP32-S3 @ 240 MHz | 8 MB | WiFi (built-in) | Default target |
| ESP32-P4-Nano (Waveshare) | ESP32-P4 @ 400 MHz | up to 32 MB | Ethernet (IP101) + WiFi (via C6) | Higher performance, more PSRAM |

---

## Features

- **SPI Flash emulation** — Winbond W25Q16 through W25Q512 (2 MB – 64 MB) and Macronix MX25L series, with correct JEDEC ID, SFDP, status registers, 4-byte addressing, page program, sector/block/chip erase
- **I2C EEPROM emulation** — 24C02 through 24C512 (256 B – 64 KB), with page boundary wrap-around per datasheet
- **4 ROM slots** in PSRAM — upload multiple images, switch between them instantly
- **Real-time web UI** — dark-themed HTMX dashboard with live access log (SSE), statistics, and ROM management
- **REST API** — fully scriptable: upload, insert, eject, monitor via `curl` or any HTTP client
- **Target control GPIOs** — reset pulse/hold (open-drain) and power on/off/cycle (MOSFET gate) via web UI or API
- **WiFi** — STA mode with stored credentials, automatic AP fallback (`ROMEMU-XXXX`), mDNS at `romemu.local`

## Supported Chips

| SPI Flash | Size | JEDEC ID | I2C EEPROM | Size |
|-----------|------|----------|------------|------|
| W25Q16JV | 2 MB | `EF 40 15` | 24C02 | 256 B |
| W25Q32JV | 4 MB | `EF 40 16` | 24C04 | 512 B |
| W25Q64JV | 8 MB | `EF 40 17` | 24C08 | 1 KB |
| W25Q128JV | 16 MB | `EF 40 18` | 24C16 | 2 KB |
| W25Q256JV | 32 MB | `EF 40 19` | 24C32 | 4 KB |
| W25Q512JV | 64 MB | `EF 40 20` | 24C64 | 8 KB |
| MX25L1606E | 2 MB | `C2 20 15` | 24C128 | 16 KB |
| MX25L3233F | 4 MB | `C2 20 16` | 24C256 | 32 KB |
| MX25L6433F | 8 MB | `C2 20 17` | 24C512 | 64 KB |
| MX25L12835F | 16 MB | `C2 20 18` | 24C1024 | 128 KB |
| MX25L25645G | 32 MB | `C2 20 19` | | |
| IS25LP064 | 8 MB | `9D 60 17` | | |
| IS25LP128 | 16 MB | `9D 60 18` | | |
| SST26VF032B | 4 MB | `BF 26 42` | | |
| SST26VF064B | 8 MB | `BF 26 43` | | |

Chips larger than available PSRAM are supported — the emulator allocates as much as it can and returns `0xFF` (erased state) for addresses beyond the allocated region. This works well for firmware development since images are typically much smaller than the full chip capacity.

## Hardware

**ESP32-S3:** Any dev board with PSRAM (e.g. ESP32-S3-DevKitC-1 with N16R8 module).
**ESP32-P4:** Waveshare ESP32-P4-Nano (has ESP32-C6 for WiFi + IP101 for Ethernet + PSRAM).

### Pin Connections

Pin assignments differ per target. Connect these pins to the target system's ROM socket:

**ESP32-S3 (IOMUX pins for SPI2/FSPI — minimum latency):**

| Function | S3 GPIO | SPI Signal |
|----------|---------|------------|
| Chip Select | 10 | CS# |
| Clock | 12 | CLK |
| MOSI / IO0 | 11 | DI |
| MISO / IO1 | 13 | DO |
| WP / IO2 | 14 | WP# |
| HD / IO3 | 9 | HOLD# |
| I2C SDA | 1 | |
| I2C SCL | 2 | |
| Reset Out | 4 | open-drain |
| Power Out | 5 | MOSFET gate |

**ESP32-P4-Nano (GPIO matrix routed):**

| Function | P4 GPIO | SPI Signal |
|----------|---------|------------|
| Chip Select | 6 | CS# |
| Clock | 8 | CLK |
| MOSI / IO0 | 7 | DI |
| MISO / IO1 | 9 | DO |
| WP / IO2 | 10 | WP# |
| HD / IO3 | 11 | HOLD# |
| I2C SDA | 12 | |
| I2C SCL | 13 | |
| Reset Out | 14 | open-drain |
| Power Out | 15 | MOSFET gate |

On ESP32-S3, GPIOs 9–14 are IOMUX pins for SPI2/FSPI, giving minimum signal propagation delay for reliable operation up to ~20–25 MHz in single SPI mode. On ESP32-P4, the 400 MHz dual-core RISC-V provides ample headroom even with GPIO matrix routing.

> **Note:** The target system must be configured to use SPI clock speeds the emulator can handle. Fast Read (0Bh) is recommended over plain Read (03h) as the dummy byte provides time for the ESP32-S3 to set up DMA from PSRAM.

## Building

### Prerequisites

- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
- Python 3

### Build & Flash

```bash
# Source ESP-IDF environment
. $IDF_PATH/export.sh

# Build for ESP32-S3 (default)
./build.sh

# Build for ESP32-P4-Nano
./build.sh esp32p4

# Flash to device
idf.py -p /dev/ttyUSB0 flash monitor
```

The `build.sh` script handles target selection, frontend embedding, source validation, and the IDF build in one step.

## Usage

### Web UI

1. On first boot, the device starts an open WiFi AP named `ROMEMU-XXXX`
2. Connect to it and navigate to `http://192.168.4.1`
3. Configure your WiFi credentials in the WiFi section — after connecting, the device is available at `http://romemu.local`
4. Upload a ROM image, select the chip type, and click **Insert** to start emulation
5. The access log shows reads/writes in real time

### API

All endpoints return JSON. Base URL: `http://romemu.local`

```bash
# Upload a firmware image to slot 0 as W25Q128
curl -X POST "http://romemu.local/api/slots/0/upload?chip=4&label=my_firmware" \
     -H "Content-Type: application/octet-stream" \
     --data-binary @firmware.bin

# Insert slot 0 (starts SPI flash emulation)
curl -X POST http://romemu.local/api/slots/0/insert

# Check status
curl http://romemu.local/api/status

# Stream live access events (SSE)
curl -N http://romemu.local/api/events

# Eject
curl -X POST http://romemu.local/api/slots/0/eject

# Reset the target (100ms pulse)
curl -X POST http://romemu.local/api/gpio/reset/pulse -d '{"duration":100}'

# Power cycle the target
curl -X POST http://romemu.local/api/gpio/power/cycle -d '{"duration":500,"settle":200}'

# Download image back
curl -o dump.bin http://romemu.local/api/slots/0/download
```

Full API reference is available in the web UI under the **API Reference** section.

<details>
<summary><strong>Chip type IDs for the API</strong></summary>

| ID | Chip | ID | Chip |
|----|------|----|------|
| 1 | W25Q16 | 9 | 24C02 |
| 2 | W25Q32 | 10 | 24C04 |
| 3 | W25Q64 | 11 | 24C08 |
| 4 | W25Q128 | 12 | 24C16 |
| 5 | W25Q256 | 13 | 24C32 |
| 6 | W25Q512 | 14 | 24C64 |
| 7 | MX25L1606E | 15 | 24C128 |
| 8 | MX25L3233F | 16 | 24C256 |
| | | 17 | 24C512 |

</details>

### API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/api/status` | System status |
| `GET` | `/api/slots` | List ROM slots |
| `POST` | `/api/slots/{n}/upload?chip={type}&label={name}` | Upload ROM (raw binary body) |
| `GET` | `/api/slots/{n}/download` | Download ROM |
| `POST` | `/api/slots/{n}/insert` | Activate emulation |
| `POST` | `/api/slots/{n}/eject` | Stop emulation |
| `DELETE` | `/api/slots/{n}` | Delete image |
| `POST` | `/api/slots/{n}/label` | Set label (`{"label":"..."}`) |
| `GET` | `/api/chip` | List supported chips |
| `POST` | `/api/chip` | Set chip type (`{"type": 4}`) |
| `GET` | `/api/stats` | R/W/erase counters |
| `POST` | `/api/stats/reset` | Reset statistics |
| `GET` | `/api/wifi` | WiFi info |
| `POST` | `/api/wifi` | Set WiFi (`{"ssid":"...","password":"..."}`) |
| `GET` | `/api/gpio` | Reset & power pin state |
| `POST` | `/api/gpio/reset/pulse` | Pulse reset (`{"duration": 100}`) |
| `POST` | `/api/gpio/reset/assert` | Hold target in reset |
| `POST` | `/api/gpio/reset/release` | Release from reset |
| `POST` | `/api/gpio/power/on` | Power on target |
| `POST` | `/api/gpio/power/off` | Power off target |
| `POST` | `/api/gpio/power/cycle` | Power cycle (`{"duration": 500, "settle": 200}`) |
| `GET` | `/api/events` | SSE stream (access log + stats) |
| `GET` | `/api/log` | Last 100 log entries |

## Architecture

```
┌─────────────────────────────────────────────────┐
│  Target System                                   │
│  (reads SPI flash / I2C EEPROM at boot)         │
└──────┬──────────────────┬───────────────────────┘
       │ SPI (GPIO 9-14)  │ I2C (GPIO 1-2)
┌──────┴──────────────────┴───────────────────────┐
│  ESP32-S3 + PSRAM                                │
│                                                   │
│  ┌─────────────┐  ┌──────────────┐               │
│  │ SPI Slave   │  │ I2C Slave    │  Emulators    │
│  │ (Core 1)    │  │ (Core 0)     │               │
│  └──────┬──────┘  └──────┬───────┘               │
│         │                │                        │
│  ┌──────┴────────────────┴───────┐               │
│  │     ROM Store (PSRAM)         │  4 slots      │
│  │     dynamic allocation        │               │
│  └───────────────┬───────────────┘               │
│                  │                                │
│  ┌───────────────┴───────────────┐               │
│  │  Access Log (ring buffer)     │  8192 entries │
│  └───────────────┬───────────────┘               │
│                  │                                │
│  ┌───────────────┴───────────────┐               │
│  │  Web Server + SSE             │  WiFi         │
│  │  REST API / HTMX UI           │               │
│  └───────────────────────────────┘               │
└──────────────────────────────────────────────────┘
```

- SPI emulation runs on **Core 1** at priority 24 for lowest latency
- I2C emulation runs on **Core 0** at priority 20
- Web server and SSE broadcast run on **Core 0**
- ROM images are stored in **PSRAM** (volatile — re-upload after power cycle)
- Access log uses a lock-free ring buffer safe for ISR producers

## Limitations

- **SPI clock speed**: Reliable up to ~20–25 MHz single SPI, ~10 MHz QSPI. The target must not exceed this.
- **PSRAM is volatile**: ROM images are lost on power cycle and must be re-uploaded (~2 seconds for 2 MB over WiFi).
- **PSRAM capacity**: The ESP32-S3 has 8 MB PSRAM. Images larger than available PSRAM are partially backed — unallocated addresses return `0xFF` (erased state). This is fine for most firmware images which are smaller than the full chip size.
- **Fast Read recommended**: Plain Read (03h) at high clock speeds may have issues on the first byte since there's no dummy cycle for the ESP32-S3 to prepare data.

## License

MIT
