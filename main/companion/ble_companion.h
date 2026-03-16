/**
 * BLE GATT companion service — streams alerts and status to a phone/watch.
 *
 * The ESP32 acts as a BLE peripheral (GATT server). A companion app
 * (or any generic BLE serial terminal) connects and receives:
 *   - Real-time alert notifications (JSON)
 *   - Periodic status updates (JSON)
 *   - Device list on request
 *
 * GATT Service UUID: 0000CYT0-0000-1000-8000-00805F9B34FB
 * Characteristics:
 *   - Alert Stream (notify):  0000CYT1 — JSON alert messages pushed on detection
 *   - Status (read/notify):   0000CYT2 — JSON status, updated every 5s
 *   - Device List (read):     0000CYT3 — JSON array of suspicious devices
 *   - Command (write):        0000CYT4 — receive commands from phone (config, etc.)
 *
 * Alerts look like normal BLE notifications — invisible to an observer.
 * The phone app renders them as standard notifications.
 *
 * BLE scanning and GATT server coexist: NimBLE supports peripheral +
 * observer roles simultaneously. Some scan packet loss (~10-15%) is expected
 * when a central is connected.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "storage/device_table.h"
#include "gps/gps_parser.h"
#include "ui/display.h"

/** Alert severity for companion notifications. */
typedef enum {
    COMPANION_ALERT_INFO = 0,
    COMPANION_ALERT_NOTABLE = 1,
    COMPANION_ALERT_ELEVATED = 2,
    COMPANION_ALERT_REVIEW = 3,
} companion_alert_level_t;

/** Initialize the BLE GATT companion service. Call after NimBLE host is synced. */
int ble_companion_init(void);

/** Start advertising so a phone can discover and connect. */
void ble_companion_start_advertising(void);

/** Stop advertising (stealth mode). */
void ble_companion_stop_advertising(void);

/** Send an alert notification to the connected companion device.
 *  No-op if no device is connected. */
void ble_companion_send_alert(companion_alert_level_t level,
                              const char *device_id,
                              float persistence_score,
                              uint8_t location_count);

/** Update the status characteristic (called periodically by analysis task). */
void ble_companion_update_status(const display_status_t *status,
                                 const gps_fix_t *gps);

/** Check if a companion device is currently connected. */
bool ble_companion_is_connected(void);

/** Command handler callback type — called when phone sends a command. */
typedef void (*companion_cmd_handler_t)(const char *cmd_json, uint16_t len);

/** Register a command handler for phone-to-device commands. */
void ble_companion_set_cmd_handler(companion_cmd_handler_t handler);
