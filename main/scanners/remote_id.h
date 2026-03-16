/**
 * Remote ID parser — extracts drone data from WiFi NAN and BLE advertisements.
 *
 * FAA Remote ID (ASTM F3411) broadcasts:
 *   - Drone serial number
 *   - Drone GPS position + altitude + speed + heading
 *   - Operator GPS position
 *   - Emergency status
 *
 * WiFi NAN: action frames on channel 6 captured in promiscuous mode.
 * BLE: parsed during BLE scan window (handled in ble_scanner).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/** Parsed Remote ID data. */
typedef struct {
    char     serial[13];        /* 12-char serial + null */
    float    drone_lat;
    float    drone_lon;
    float    drone_alt;
    float    drone_speed;
    float    operator_lat;
    float    operator_lon;
    uint8_t  emergency_status;
    bool     valid;
} remote_id_data_t;

/**
 * Try to parse a WiFi NAN action frame as Remote ID.
 * Returns true if the frame contains valid Remote ID data.
 */
bool remote_id_parse_wifi_nan(const uint8_t *frame, uint16_t len, remote_id_data_t *out);

/**
 * Try to parse BLE service data as Remote ID.
 * Returns true if the data contains valid Remote ID.
 */
bool remote_id_parse_ble(const uint8_t *service_data, uint16_t len, remote_id_data_t *out);
