/**
 * Device tracking table — fixed-size records in PSRAM with SRAM hash index.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/** Source type for a device detection. */
typedef enum {
    SOURCE_WIFI = 0,
    SOURCE_BLE  = 1,
    SOURCE_TPMS = 2,
    SOURCE_DRONE = 3,
} source_type_t;

/**
 * Fixed-size device record — 48 bytes, stored in PSRAM.
 *
 * For WiFi: device_id = MAC (6 bytes).
 * For BLE:  device_id = payload hash (first 6 bytes of SHA-256).
 * For TPMS: device_id = sensor ID (4 bytes, zero-padded).
 * For Drone: device_id = serial hash (first 6 bytes).
 */
typedef struct __attribute__((packed)) {
    uint8_t  device_id[6];       /* Primary key — 6 bytes */
    uint8_t  ssid[33];           /* Probed SSID (WiFi only, null-terminated) */
    uint8_t  ssid_len;           /* SSID length */
    int8_t   rssi_avg;           /* Average RSSI (dBm) */
    uint8_t  appearance_count;   /* Total appearances this session (max 255) */
    uint8_t  window_flags;       /* Bit 0=5min, 1=10min, 2=15min, 3=20min */
    uint32_t first_seen;         /* Epoch seconds */
    uint32_t last_seen;          /* Epoch seconds */
    uint8_t  source_type;        /* source_type_t */
    uint8_t  _padding;           /* Alignment */
} device_record_t;

_Static_assert(sizeof(device_record_t) == 48, "device_record_t must be 48 bytes");

/** Initialize the device table (allocates PSRAM). Returns 0 on success. */
int device_table_init(void);

/** Look up a device by its 6-byte ID. Returns pointer or NULL. */
device_record_t *device_table_lookup(const uint8_t id[6]);

/** Insert or update a device record. Returns pointer to the record. */
device_record_t *device_table_upsert(const uint8_t id[6], source_type_t source);

/** Rotate time window flags: shift right by 1, clear bit 0. */
void device_table_rotate_windows(void);

/** Get count of active devices (appearance_count > 0). */
uint32_t device_table_active_count(void);

/** Get count of devices with multiple window bits set (suspicious). */
uint32_t device_table_suspicious_count(void);

/** Iterate all devices with the given minimum window_flags bits set. */
typedef void (*device_callback_t)(const device_record_t *record, void *ctx);
void device_table_for_each_suspicious(uint8_t min_bits, device_callback_t cb, void *ctx);
