# CYT-NG Handheld

ESP32-S3 portable surveillance detection device. Companion to the [CYT-NG base station](https://github.com/robomoto/Chasing-Your-Tail-NG).

## Hardware

- **MCU:** LilyGo T-Display-S3 (non-touch, 8MB PSRAM)
- **Sub-GHz:** CC1101 module (315/433 MHz — TPMS, OOK/FSK)
- **GPS:** u-blox NEO-M8N (UART)
- **Storage:** microSD card (SPI)
- **Display:** 1.9" 170x320 ST7789 TFT
- **Battery:** 18650 3000mAh (~13h runtime)
- **BOM:** ~$58

## Capabilities

| Scanner | Frequency | Detects |
|---------|-----------|---------|
| WiFi promiscuous | 2.4 GHz | Probe requests, persistent WiFi devices |
| BLE GAP scan | 2.4 GHz | AirTags, SmartTags, Tile, AirPods, Google Find My |
| Remote ID | 2.4 GHz (BLE + WiFi NAN) | Drone serial number + operator GPS |
| CC1101 sub-GHz | 315/433 MHz | TPMS tire sensors, key fobs, OOK devices |
| GPS | 1575 MHz | Location tagging for multi-location correlation |

## Architecture

```
Core 0: WiFi/BLE radio          Core 1: Application
  Promiscuous callback             Analysis task (dequeue, hash, score)
  Channel hop task                 Display task (TFT refresh)
  BLE scan task                    Logger task (SD card batch writes)
  (alternating windows)            GPS task (UART NMEA parsing)

  CC1101 sub-GHz: continuous on SPI (separate hardware, no radio conflict)
```

## Build (Docker)

```bash
# Build firmware
docker compose run build

# Flash to device (set USB device in docker-compose.yml first)
docker compose run flash

# Monitor serial output
docker compose run monitor

# Interactive menuconfig
docker compose run menuconfig
```

## Session Export

Sessions are logged to SD card as CSV, compatible with the base station's `HandheldImporter`:

```csv
# session_id=<uuid>,fw_ver=1.0.0,start=<epoch>,end=<epoch>,device_count=N
timestamp,mac,device_id,source_type,rssi,lat,lon,ssid,window_flags,appearance_count
```

Upload to base station via USB-C cable or SD card swap.

## Documentation

- [Hardware BOM & Pin Map](docs/hardware.md) — complete parts list, GPIO assignments, power budget
- [TODO](docs/TODO.md) — remaining firmware work and v2 enhancements
- [Phone App Requirements](docs/phone-app-requirements.md) — companion app spec (separate repo)

## Related Projects

- [CYT-NG Base Station](https://github.com/robomoto/Chasing-Your-Tail-NG) — Raspberry Pi multi-sensor base station (Python)
- Companion App — planned (see [requirements](docs/phone-app-requirements.md))

## License

MIT
