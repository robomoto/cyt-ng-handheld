# ESP32 Handheld: Hardware Plan

## Full-Featured Build (~$58)

### Core: LilyGo T-Display-S3

**LilyGo T-Display-S3 Non-Touch, 8MB PSRAM, 16MB flash** — ~$18

- Dual-core Xtensa LX7 @ 240 MHz
- 512KB SRAM + 8MB PSRAM (octal SPI)
- 1.9" 170x320 ST7789 TFT
- BLE 5.0 for tracker detection + Remote ID
- WiFi promiscuous mode for probe capture
- Native USB-C (USB-OTG) for charging + data upload
- Built-in LiPo charging circuit

**CRITICAL: Buy the NON-TOUCH variant.** The touch version uses GPIO 1-3 which conflict with the SPI bus.

### Bill of Materials

| # | Component | Specific Part | Connection | Cost |
|---|-----------|--------------|------------|------|
| 1 | MCU + Display | LilyGo T-Display-S3 (8MB PSRAM, non-touch) | — | $18 |
| 2 | GPS module | u-blox NEO-M8N (GY-NEO8MV2 breakout) | UART1: GPIO 43 (TX), GPIO 44 (RX) | $8 |
| 3 | SD card breakout | Generic microSD SPI module (3.3V native, no level shifter) | SPI: GPIO 10 (CS), 11 (MOSI), 12 (CLK), 13 (MISO) | $2 |
| 4 | microSD card | 8GB+ Class 10 | In SD breakout | $4 |
| 5 | Sub-GHz module | CC1101-based (EBYTE E07-900M10S or generic) | SPI shared: GPIO 9 (CS), 8 (GDO0/IRQ), 7 (GDO2), 6 (optional RST) | $6 |
| 6 | 433 MHz antenna | 1/4 wave SMA whip (~17cm) | SMA on CC1101 | $3 |
| 7 | 2.4 GHz antenna | External SMA whip + U.FL pigtail | Solder to PCB antenna feed | $3 |
| 8 | Battery | 18650 Samsung 30Q (3000mAh) + spring holder w/ JST-PH 2.0 | JST-PH to battery connector | $5 |
| 9 | Buzzer | Passive piezo (3.3V) | GPIO 1 (PWM) | $1 |
| 10 | Buttons | 3x tactile switches (6mm) | GPIO 2, 3, 14 (w/ 10K pull-ups) | $1 |
| 11 | Enclosure | 3D-printed or Hammond 1591XXBSBK (~100x60x30mm) | — | $5 |
| 12 | Wiring/misc | JST connectors, perfboard, headers, SMA pigtails | — | $5 |
| | **TOTAL** | | | **~$58** |

### Budget Options

| Build | What's Removed | Cost |
|-------|---------------|------|
| Without CC1101 | Remove items 5, 6 | ~$49 |
| Bare minimum | Remove LoRa, ext antenna, buzzer, extra buttons | ~$38 |

## GPIO Pin Map

| GPIO | Assignment | Bus |
|------|-----------|-----|
| 1 | Buzzer (PWM) | LEDC |
| 2 | Button 1 (scroll up) | Digital input |
| 3 | Button 2 (scroll down / select) | Digital input |
| 6 | LoRa BUSY | Digital input |
| 7 | LoRa DIO1 (IRQ) | Digital input (interrupt) |
| 8 | LoRa RST | Digital output |
| 9 | LoRa CS | SPI chip select |
| 10 | SD Card CS | SPI chip select |
| 11 | SPI MOSI (shared: SD + LoRa) | SPI3 |
| 12 | SPI CLK (shared: SD + LoRa) | SPI3 |
| 13 | SPI MISO (shared: SD + LoRa) | SPI3 |
| 14 | Button 3 (back / mode) | Digital input |
| 18-19 | USB D-/D+ (OTG) | Reserved |
| 43 | GPS UART TX | UART1 |
| 44 | GPS UART RX | UART1 |

**SPI bus sharing:** SD card and LoRa module share MOSI/MISO/CLK with separate CS lines. Firmware must use SPI bus mutex. Standard pattern — ESP-IDF handles this natively.

**Free GPIOs after full build:** 0-2 (no expansion headroom with LoRa installed)

## Power Budget

| Component | Active (mA) | Duty Cycle | Average (mA) |
|-----------|------------|------------|--------------|
| ESP32-S3 WiFi promiscuous RX | 130 | 75% | 97.5 |
| ESP32-S3 BLE scanning | 100 | 15% | 15 |
| ESP32-S3 CPU (dual-core, 240MHz) | 50 | 100% | 50 |
| CC1101 sub-GHz RX (continuous) | 15 | 100% | 15 |
| GPS module (NEO-M8N) | 30 | 50% | 15 |
| TFT display (backlight on) | 25 | 40% | 10 |
| SD card (SPI writes) | 40 | 2% | 0.8 |
| Buzzer (intermittent) | 30 | 0.5% | 0.15 |
| Regulator overhead (~15%) | — | 100% | 29 |
| **Total** | | | **~224 mA** |

### Battery Life

| Battery | Capacity | Runtime | Notes |
|---------|----------|---------|-------|
| 18650 (stock settings) | 3000mAh | ~13.4h | WiFi + BLE + LoRa + GPS + display |
| 18650 (optimized) | 3000mAh | ~15h+ | Display auto-off, GPS 60s cycle, CPU 160MHz idle |
| LiPo 2000mAh (compact) | 2000mAh | ~9h | Smaller form factor option |

CC1101 draws ~15mA in RX — minimal power impact.

## Antenna Strategy

| Band | Antenna | Notes |
|------|---------|-------|
| 2.4 GHz (WiFi/BLE) | External SMA whip via U.FL pigtail ($3) | Improves BLE tracker range to ~30-40m vs ~10-20m with PCB antenna |
| 433 MHz (TPMS/sub-GHz) | 1/4 wave SMA whip ~17cm ($3) | On CC1101 module. TPMS range: ~30-50m |
| GPS (1575 MHz) | Ceramic patch (included with NEO-M8N) | Position on top of enclosure, facing sky |

Two SMA bulkhead connectors on the enclosure (top edge). Both antennas protrude upward.

## Capabilities

| Capability | How |
|-----------|-----|
| WiFi probe capture (2.4 GHz) | ESP32 promiscuous mode |
| BLE tracker detection (AirTag, SmartTag, Tile) | ESP32 BLE GAP scan |
| Remote ID (BLE) | ESP32 BLE scan for ASTM F3411 advertisements |
| Remote ID (WiFi NAN) | ESP32 promiscuous mode captures NAN action frames |
| TPMS vehicle tracking | CC1101 module, decode 315/433 MHz OOK/FSK tire sensors |
| Sub-GHz device detection | CC1101 decodes key fobs, security sensors, simple OOK protocols |
| GPS location tagging | UART GPS module |
| Session logging | CSV to SD card |
| Real-time alerts | TFT display + buzzer |
| Data upload to base station | USB-C serial (primary) or SD card swap (fallback) |

## What the Handheld CANNOT Do (Base Station Only)

- 5 GHz WiFi monitoring
- Full rtl_433 protocol decoding (200+ protocols — handheld CC1101 covers TPMS + basic OOK/FSK only)
- LoRa/Meshtastic decoding (would need SX1262 — swapped for CC1101)
- ADS-B aircraft tracking
- RF wideband sweep
- WiGLE API queries
- KML/HTML report generation
- Historical cross-session analysis

## RTL-SDR via USB-OTG: Not Feasible

Evaluated and rejected:
- No ESP32 port of librtlsdr
- 300mA power draw cuts battery life to ~5 hours
- ESP32 cannot process 2.048 Msps I/Q samples in real-time
- Destroys handheld form factor
- The SX1262 LoRa module covers the most important sub-GHz use case at 1/50th the power

## Open Decisions

- [ ] Enclosure design — accommodate two SMA antenna connectors + battery compartment
- [ ] SPI bus verification — confirm shared SPI works reliably between SD + SX1262 on target board revision
- [ ] T-Display-S3 board revision — verify GPIO 6-14 are all broken out on headers
- [ ] Optional vibration motor — swap for or add alongside buzzer for silent alerts
