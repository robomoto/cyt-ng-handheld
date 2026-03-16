/**
 * CC1101 sub-GHz scanner — TPMS and OOK/FSK protocol decoding.
 *
 * Listens continuously on 315 MHz (US) or 433.92 MHz (EU) for:
 *   - TPMS tire pressure sensors (unique 28-32 bit sensor IDs)
 *   - Key fob transmissions
 *   - Basic OOK security sensors
 *
 * The CC1101 operates on its own SPI CS line, sharing the bus with the SD card.
 * It runs independently of the WiFi/BLE radio (separate hardware).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/** Decoded sub-GHz packet. */
typedef struct {
    uint8_t  device_id[6];       /* Sensor ID, zero-padded to 6 bytes */
    char     protocol[16];       /* "TPMS", "keyfob", "ook_sensor", etc. */
    uint32_t raw_id;             /* Original sensor ID value */
    int8_t   rssi;
    float    pressure_psi;       /* TPMS only, 0.0 if not applicable */
    float    temperature_c;      /* TPMS only, 0.0 if not applicable */
    bool     is_tpms;            /* True if this is a TPMS detection */
    uint32_t timestamp;
} cc1101_detection_t;

/** Initialize CC1101 hardware (SPI, configure registers). Returns 0 on success. */
int cc1101_scanner_init(void);

/** Start continuous RX on configured frequency. */
void cc1101_scanner_start(void);

/** Stop RX. */
void cc1101_scanner_stop(void);

/** Set RX frequency in MHz (315.0 or 433.92). */
void cc1101_scanner_set_frequency(float freq_mhz);

/** Returns the FreeRTOS queue handle for dequeuing cc1101_detection_t items. */
void *cc1101_scanner_get_queue(void);
