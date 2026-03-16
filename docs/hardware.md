# ESP32 Handheld: Hardware Plan

## Full-Featured Build (~$62)

### Core: LilyGo T-Display-S3 AMOLED

**LilyGo T-Display-S3 AMOLED, 8MB PSRAM, 16MB flash** — ~$22

- Dual-core Xtensa LX7 @ 240 MHz
- 512KB SRAM + 8MB PSRAM (octal SPI)
- 1.91" 240x536 RM67162 AMOLED (QSPI interface)
- BLE 5.0 for tracker detection + Remote ID
- WiFi promiscuous mode for probe capture
- Native USB-C (USB-OTG) for charging + data upload
- Built-in LiPo charging circuit
- Perfect blacks — background-color-as-threat-level is more effective
- Better readability at all viewing angles and in low light
- Lower power on dark themes (only lit pixels draw current)
- 30 chars/line × 33 lines (vs 21×20 on TFT) — alert text fits without wrapping

### Bill of Materials


| #   | Component        | Specific Part                                              | Connection                                                        | Cost     |
| --- | ---------------- | ---------------------------------------------------------- | ----------------------------------------------------------------- | -------- |
| 1   | MCU + Display    | LilyGo T-Display-S3 AMOLED (8MB PSRAM)                     | —                                                                 | $22      |
| 2   | GPS module       | u-blox NEO-M8N (GY-NEO8MV2 breakout)                       | UART1: GPIO 43 (TX), GPIO 44 (RX)                                 | $8       |
| 3   | SD card breakout | Generic microSD SPI module (3.3V native, no level shifter) | SPI: GPIO 10 (CS), 11 (MOSI), 12 (CLK), 13 (MISO)                 | $2       |
| 4   | microSD card     | 8GB+ Class 10                                              | In SD breakout                                                    | $4       |
| 5   | Sub-GHz module   | CC1101-based (EBYTE E07-900M10S or generic)                | SPI shared: GPIO 15 (CS), 16 (GDO0/IRQ), 21 (GDO2) | $6       |
| 6   | 433 MHz antenna  | 1/4 wave SMA whip (~17cm)                                 | SMA on CC1101 module                                              | $3       |
| 7   | 2.4 GHz antenna  | External SMA whip + U.FL pigtail                          | U.FL to board PCB antenna pad                                     | $3       |
| 8   | Battery          | 18650 Samsung 30Q (3000mAh) + spring holder w/ JST-PH 2.0  | JST-PH to battery connector                                       | $5       |
| 9   | Buzzer           | Passive piezo (3.3V)                                       | GPIO 1 (PWM)                                                      | $1       |
| 10  | Buttons          | 3x tactile switches (6mm)                                  | GPIO 2, 3, 14 (w/ 10K pull-ups)                                   | $1       |
| 11  | Enclosure        | 3D-printed (~120x65x35mm)                                  | 2x SMA bulkhead connectors on top edge                            | $5       |
| 12  | Wiring/misc      | JST connectors, perfboard, headers, SMA pigtails           | —                                                                 | $5       |
|     | **TOTAL**        |                                                            |                                                                   | **~$67** |


**Antennas:** Two external SMA whip antennas on bulkhead connectors through the enclosure top. The 2.4 GHz whip (~3cm) connects via U.FL pigtail to the board's antenna pad, improving BLE tracker range to ~30-40m. The 433 MHz whip (~17cm) connects to the CC1101 module for ~50m TPMS range. GPS ceramic patch is internal to the NEO-M8N module.

### Budget Options


| Build                        | What's Removed                       | Cost |
| ---------------------------- | ------------------------------------ | ---- |
| Without CC1101               | Remove items 5, 6                    | ~$58 |
| Without ext antennas         | Remove item 7, use PCB/internal wire | ~$64 |
| Bare minimum (WiFi+BLE only) | Remove CC1101, antennas, buzzer, extra buttons | ~$45 |


## Display Comparison


|                    | Old (TFT)                | New (AMOLED)              |
| ------------------ | ------------------------ | ------------------------- |
| Size               | 1.9"                     | 1.91"                     |
| Resolution         | 170×320                  | 240×536                   |
| Pixels             | 54,400                   | 128,640 (2.4× more)       |
| Text (8×16 font)   | 21 chars × 20 lines      | 30 chars × 33 lines       |
| Driver IC          | ST7789 (parallel i80)    | RM67162 (QSPI)            |
| Contrast           | Good                     | Perfect blacks            |
| Viewing angle      | Washes out               | Excellent                 |
| Power (dark theme) | ~25mA constant backlight | ~5-10mA (only lit pixels) |
| Price              | $18                      | $22                       |


## GPIO Pin Map


| GPIO  | Assignment                      | Bus                       |
| ----- | ------------------------------- | ------------------------- |
| 1     | Buzzer (PWM)                    | LEDC                      |
| 2     | Button 1 (scroll up)            | Digital input             |
| 3     | Button 2 (scroll down / select) | Digital input             |
| 15    | CC1101 CS                       | SPI chip select           |
| 16    | CC1101 GDO0 (IRQ)               | Digital input (interrupt) |
| 21    | CC1101 GDO2                     | Digital input             |
| 10    | SD Card CS                      | SPI chip select           |
| 11    | SPI MOSI (shared: SD + CC1101)  | SPI3                      |
| 12    | SPI CLK (shared: SD + CC1101)   | SPI3                      |
| 13    | SPI MISO (shared: SD + CC1101)  | SPI3                      |
| 14    | Button 3 (back / mode)          | Digital input             |
| 18-19 | USB D-/D+ (OTG)                 | Reserved                  |
| 43    | GPS UART TX                     | UART1                     |
| 44    | GPS UART RX                     | UART1                     |


**SPI bus sharing:** SD card and CC1101 share MOSI/MISO/CLK with separate CS lines. Firmware uses SPI bus mutex.

**AMOLED display pins:** The T-Display-S3 AMOLED uses internal QSPI for the display (not user-accessible GPIOs). Display does NOT consume any of the above pins.

**Free GPIOs after full build:** 0-2 depending on board revision.

## Power Budget


| Component                                    | Active (mA) | Duty Cycle | Average (mA) |
| -------------------------------------------- | ----------- | ---------- | ------------ |
| ESP32-S3 WiFi promiscuous RX                 | 130         | 75%        | 97.5         |
| ESP32-S3 BLE scanning                        | 100         | 15%        | 15           |
| ESP32-S3 CPU (dual-core, 240MHz)             | 50          | 100%       | 50           |
| CC1101 sub-GHz RX (continuous)               | 15          | 100%       | 15           |
| GPS module (NEO-M8N)                         | 30          | 50%        | 15           |
| AMOLED display (dark theme, ~10% lit pixels) | 8           | 40%        | 3.2          |
| SD card (SPI writes)                         | 40          | 2%         | 0.8          |
| Buzzer (intermittent)                        | 30          | 0.5%       | 0.15         |
| Regulator overhead (~15%)                    | —           | 100%       | 27           |
| **Total**                                    |             |            | **~216 mA**  |


### Battery Life


| Battery                | Capacity | Runtime | Notes                                            |
| ---------------------- | -------- | ------- | ------------------------------------------------ |
| 18650 (stock settings) | 3000mAh  | ~13.9h  | AMOLED saves ~8mA vs TFT backlight               |
| 18650 (optimized)      | 3000mAh  | ~16h+   | Display auto-off, GPS 60s cycle, CPU 160MHz idle |
| LiPo 2000mAh (compact) | 2000mAh  | ~9.3h   | Smaller form factor option                       |


AMOLED with dark theme actually uses LESS power than TFT — the black background pixels draw zero current.

## Antenna Strategy


| Band                   | Antenna                                       | Notes                                                                                                           |
| ---------------------- | --------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| 2.4 GHz (WiFi/BLE)     | External SMA whip (~3cm) via U.FL pigtail     | SMA bulkhead connector on enclosure. Range ~30-40m for BLE trackers, ~40-50m for WiFi probes. |
| 433 MHz (TPMS/sub-GHz) | External SMA whip (~17cm)                     | SMA bulkhead connector on enclosure. Range ~50m for TPMS. |
| GPS (1575 MHz)         | Ceramic patch (inside NEO-M8N module)         | Position module near top of enclosure for sky view. |


**Two external SMA antennas** on bulkhead connectors through the enclosure top edge. Better range is worth the visibility — the device lives in a bag and two small whip antennas are unremarkable.

## Capabilities


| Capability                                     | How                                                             |
| ---------------------------------------------- | --------------------------------------------------------------- |
| WiFi probe capture (2.4 GHz)                   | ESP32 promiscuous mode                                          |
| BLE tracker detection (AirTag, SmartTag, Tile) | ESP32 BLE GAP scan                                              |
| Remote ID (BLE)                                | ESP32 BLE scan for ASTM F3411 advertisements                    |
| Remote ID (WiFi NAN)                           | ESP32 promiscuous mode captures NAN action frames               |
| TPMS vehicle tracking                          | CC1101 module, decode 315/433 MHz OOK/FSK tire sensors          |
| Sub-GHz device detection                       | CC1101 decodes key fobs, security sensors, simple OOK protocols |
| GPS location tagging                           | UART GPS module                                                 |
| Session logging                                | CSV to SD card                                                  |
| Real-time alerts                               | AMOLED display + buzzer                                         |
| Phone companion                                | BLE GATT server streams alerts/status to phone app              |
| Data upload to base station                    | USB-C serial (primary) or SD card swap (fallback)               |


## What the Handheld CANNOT Do (Base Station Only)

- 5 GHz WiFi monitoring
- Full rtl_433 protocol decoding (200+ protocols — handheld CC1101 covers TPMS + basic OOK/FSK only)
- LoRa/Meshtastic decoding (would need SX1262 — swapped for CC1101)
- ADS-B aircraft tracking
- RF wideband sweep
- WiGLE API queries
- KML/HTML report generation
- Historical cross-session analysis

## Open Decisions

- Verify T-Display-S3 AMOLED GPIO availability matches pin map above
- Enclosure design — no external antennas, accommodate 18650 holder + perfboard
- SPI bus verification — confirm shared SPI works reliably between SD + CC1101
- Internal 433 MHz wire antenna tuning — verify TPMS reception range with coiled wire
- Optional vibration motor — swap for or add alongside buzzer for silent alerts

