/**
 * GPS NMEA parser — reads from UART-connected u-blox NEO-M8N.
 *
 * Parses $GPRMC and $GPGGA sentences for position, speed, and time.
 * GPS time is used to set the ESP32 RTC for accurate timestamps.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/** Current GPS fix data. */
typedef struct {
    float    latitude;
    float    longitude;
    float    altitude_m;
    float    speed_knots;
    uint32_t utc_timestamp;     /* Unix epoch seconds from GPS */
    bool     has_fix;
    uint8_t  satellites;
} gps_fix_t;

/** Initialize GPS UART. Returns 0 on success. */
int gps_init(void);

/** Get the latest GPS fix. Thread-safe (reads a copy). */
gps_fix_t gps_get_fix(void);

/** Returns true if GPS has achieved a fix since boot. */
bool gps_has_fix(void);

/** Get current timestamp — GPS-derived if available, uptime otherwise. */
uint32_t gps_get_timestamp(void);
