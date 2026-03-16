/**
 * BLE scanner — detects AirTags, SmartTags, Tile, Remote ID.
 *
 * Uses NimBLE GAP scanning. Classifies advertisements by manufacturer ID:
 *   - Apple 0x004C type 0x12 (≥25 bytes) → AirTag / Find My
 *   - Apple 0x004C type 0x07 → AirPods / Find My Nearby
 *   - Samsung 0x0075 (≥4 bytes) → SmartTag
 *   - Google 0x00E0 → Find My Device
 *   - Tile service UUID containing "feed" → Tile
 *   - ASTM F3411 Remote ID → drone serial + operator GPS
 *
 * BLE scanning alternates with WiFi: 45s WiFi / 10s BLE.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "storage/device_table.h"

/** Classification result from a BLE advertisement. */
typedef struct {
    uint8_t  device_id[6];      /* Payload hash (first 6 bytes of SHA-256) */
    char     tracker_type[16];  /* "findmy", "smarttag", "tile", "drone", etc. */
    int8_t   rssi;
    uint8_t  mac[6];            /* Rotating BLE MAC (for logging only) */
    /* Remote ID specific */
    bool     is_remote_id;
    char     drone_serial[13];  /* 12 chars + null */
    float    drone_lat;
    float    drone_lon;
    float    drone_alt;
    float    operator_lat;
    float    operator_lon;
} ble_detection_t;

/** Start BLE scanning (call after WiFi scan window ends). */
void ble_scanner_start(void);

/** Stop BLE scanning (call before WiFi scan window starts). */
void ble_scanner_stop(void);

/** Returns the FreeRTOS queue handle for dequeuing ble_detection_t items. */
void *ble_scanner_get_queue(void);
