/**
 * TFT display driver — 170x320 ST7789 on T-Display-S3.
 *
 * Three screens navigated by 3 buttons:
 *   Screen 0: Status (device count, alert count, battery, GPS fix)
 *   Screen 1: Alert detail (highest-persistence device info)
 *   Screen 2: Device list (scrollable, 2 lines per device)
 *
 * Updated every 2 seconds from the analysis task on Core 1.
 * Auto-dims after 30 seconds of inactivity.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/** Screen IDs. */
typedef enum {
    SCREEN_STATUS = 0,
    SCREEN_ALERT  = 1,
    SCREEN_DEVICES = 2,
    SCREEN_COUNT,
} screen_id_t;

/** Display status data (populated by analysis task). */
typedef struct {
    uint32_t total_devices;
    uint32_t suspicious_count;
    uint32_t wifi_count;
    uint32_t ble_count;
    uint32_t tpms_count;
    uint32_t drone_count;
    float    highest_persistence;
    char     highest_device_id[20];
    bool     gps_fix;
    uint8_t  battery_percent;
    bool     sd_ready;
    bool     session_active;
} display_status_t;

/** Initialize the display hardware. */
void display_init(void);

/** Update display with new status data. Thread-safe. */
void display_update(const display_status_t *status);

/** Switch to the next screen (button press handler). */
void display_next_screen(void);

/** Switch to previous screen. */
void display_prev_screen(void);

/** Wake the display (resets auto-off timer). */
void display_wake(void);

/** Force display off (stealth). */
void display_off(void);
