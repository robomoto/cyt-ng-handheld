# CYT-NG Handheld: TODO

## Firmware Status

All modules have initial implementations. The firmware needs Docker build verification and hardware testing before it's production-ready.

### Build Verification
- [ ] Verify Docker build compiles cleanly (`docker compose run build`)
- [ ] Resolve any ESP-IDF v5.2 API changes (esp_lcd, NimBLE, etc.)
- [ ] Verify partition table fits (3MB app + 960KB storage)
- [ ] Test flash to actual T-Display-S3 hardware

### WiFi Scanner
- [ ] Verify promiscuous callback performance under load (busy environment)
- [ ] Test channel hop timing accuracy
- [ ] Validate SSID extraction edge cases (hidden SSIDs, non-ASCII)
- [ ] Measure packet drop rate at various queue depths

### BLE Scanner
- [ ] Test with real AirTag, SmartTag, Tile devices
- [ ] Verify payload hash stability across MAC rotations
- [ ] Test NimBLE observer + peripheral coexistence (companion connected while scanning)
- [ ] Measure BLE scan window packet capture rate
- [ ] Test Remote ID with real drone (DJI, Skydio)

### CC1101 Sub-GHz
- [ ] Verify SPI communication with actual CC1101 module
- [ ] Test TPMS decoding with real vehicle tire sensors
- [ ] Add support for 315 MHz (US TPMS) — current default is 433.92 MHz
- [ ] Expand protocol support beyond Schrader/Pacific/TRW preambles
- [ ] Test SPI bus sharing between CC1101 and SD card under load

### GPS
- [ ] Test with real NEO-M8N module
- [ ] Verify RTC set from GPS time
- [ ] Test cold start time to first fix
- [ ] Validate NMEA parsing with multi-constellation (GLONASS, Galileo) sentences

### SD Card Logger
- [ ] Test CSV output compatibility with base station HandheldImporter
- [ ] Verify 64KB buffer flush under SPI contention
- [ ] Test session file naming with GPS-derived timestamps
- [ ] Test SD card hot-swap behavior

### Display
- [ ] Verify T-Display-S3 specific pin assignments for ST7789
- [ ] Test bitmap font rendering at 170x320
- [ ] Implement button interrupt handlers (currently no ISR setup)
- [ ] Add battery percentage reading (ADC on T-Display-S3)
- [ ] Test auto-off timer and wake behavior

### BLE Companion Service
- [ ] Test GATT service discovery from phone (nRF Connect app)
- [ ] Verify JSON alert notifications reach phone
- [ ] Test simultaneous BLE scanning + GATT server
- [ ] Measure impact on scan packet loss with companion connected
- [ ] Test reconnection behavior after connection drop

### Device Table
- [ ] Stress test with 10,000 devices in PSRAM
- [ ] Measure hash collision rate with real-world MAC/ID distributions
- [ ] Verify window rotation correctness over extended periods
- [ ] Test memory fragmentation after long runtime

### Integration
- [ ] End-to-end test: WiFi probe → device table → alert → companion notification
- [ ] Power consumption measurement (target: <230mA average)
- [ ] Battery life test (target: 13+ hours on 3000mAh 18650)
- [ ] Thermal test during continuous scanning
- [ ] USB serial upload to base station verification

## Future Enhancements (v2)

- [ ] Stealth mode: display shows clock/calculator, obfuscated BLE name
- [ ] Quick-wipe button combo (hold all 3 buttons for 5s)
- [ ] OTA firmware updates via WiFi AP mode
- [ ] Adaptive channel hopping (weight channels producing actual probes)
- [ ] BLE tracker correlation table with TTL pruning (distinguish multiple nearby AirTags)
- [ ] Power management: duty cycling, light sleep between scan windows
- [ ] Custom companion app (Flutter/React Native) — see docs/phone-app-requirements.md
