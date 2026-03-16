/**
 * Familiar device store — NVS-backed list of known/trusted device IDs.
 *
 * Three ways devices get marked familiar:
 *   1. Automatic baseline: first 5 minutes after boot, all detected devices
 *      are marked familiar with source_type hint and auto_baselined=true.
 *      User can review and un-mark any of these.
 *   2. Phone companion: BLE GATT command from phone app.
 *   3. Handheld UI: long-press on device in device list.
 *
 * Stored in NVS (non-volatile storage) so the list survives reboots.
 * Each entry stores:
 *   - 6-byte device_id (the hash/MAC used as primary key)
 *   - source_type hint (WiFi/BLE/TPMS/Drone) for display context
 *   - device_hint: human-readable type description derived from signal:
 *       WiFi:  "WiFi Device" or SSID if captured (e.g., "WiFi: MyPhone")
 *       BLE:   "AirTag", "SmartTag", "Tile", "AirPods", "BLE Device"
 *       TPMS:  "Tire Sensor (XXXX)" with last 4 hex of sensor ID
 *       Drone: "Drone: <serial>"
 *   - auto_baselined: true if learned during first-boot baseline
 *   - user_label: optional user-assigned name (set via phone app)
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "storage/device_table.h"

/** Maximum number of familiar devices stored in NVS. */
#define FAMILIAR_MAX_DEVICES    256

/** Maximum length of device hint string. */
#define FAMILIAR_HINT_LEN       32

/** Maximum length of user-assigned label. */
#define FAMILIAR_LABEL_LEN      24

/** A single familiar device entry. */
typedef struct __attribute__((packed)) {
    uint8_t      device_id[6];
    uint8_t      source_type;                  /* source_type_t */
    char         device_hint[FAMILIAR_HINT_LEN]; /* e.g., "AirTag", "Tire Sensor (A1B2)" */
    char         user_label[FAMILIAR_LABEL_LEN]; /* e.g., "My Car", "Partner's AirPods" */
    uint8_t      auto_baselined;               /* 1 = learned during baseline */
} familiar_entry_t;

/** Initialize the familiar device store (loads from NVS). Returns 0 on success. */
int familiar_init(void);

/** Check if a device_id is in the familiar list. */
bool familiar_is_known(const uint8_t device_id[6]);

/** Add a device to the familiar list with type hint.
 *  If already present, updates the hint/label. */
void familiar_add(const uint8_t device_id[6], source_type_t source,
                  const char *device_hint, bool auto_baselined);

/** Remove a device from the familiar list. */
void familiar_remove(const uint8_t device_id[6]);

/** Set user-assigned label for a familiar device (from phone companion). */
void familiar_set_label(const uint8_t device_id[6], const char *label);

/** Get the familiar entry for a device, or NULL if not familiar. */
const familiar_entry_t *familiar_get(const uint8_t device_id[6]);

/** Get the total count of familiar devices. */
uint32_t familiar_count(void);

/** Get count of auto-baselined devices (for review prompt). */
uint32_t familiar_baseline_count(void);

/** Iterate all familiar devices (for phone companion device list / review). */
typedef void (*familiar_callback_t)(const familiar_entry_t *entry, void *ctx);
void familiar_for_each(familiar_callback_t cb, void *ctx);

/** Save current state to NVS. Called automatically on add/remove,
 *  but can be called manually to force a write. */
void familiar_save(void);

/* ── Baseline learning mode ──────────────────────────────────────── */

/** Start baseline learning. All devices detected during this period
 *  are automatically marked familiar with auto_baselined=true. */
void familiar_start_baseline(void);

/** Stop baseline learning. Returns count of devices learned. */
uint32_t familiar_stop_baseline(void);

/** Check if baseline learning is currently active. */
bool familiar_is_baseline_active(void);
