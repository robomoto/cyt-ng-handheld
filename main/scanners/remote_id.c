/**
 * Remote ID parser — ASTM F3411 Open Drone ID.
 *
 * Parses drone identification data from BLE service data and WiFi NAN
 * action frames. The binary format uses a 0x0F marker byte followed by
 * serial number, GPS coordinates, altitude, speed, and operator position.
 */

#include "remote_id.h"
#include <string.h>
#include <stdint.h>

#define REMOTE_ID_MARKER    0x0F
#define REMOTE_ID_MIN_LEN   37

/* ── Helpers ─────────────────────────────────────────────────────── */

/**
 * Read a little-endian float32 from a byte buffer.
 */
static float read_float32_le(const uint8_t *buf)
{
    float val;
    memcpy(&val, buf, sizeof(float));
    return val;
}

/**
 * Parse the common Remote ID binary payload starting at the marker byte.
 * Layout:
 *   [0]      0x0F marker
 *   [1..12]  serial (12 ASCII chars)
 *   [13..16] drone_lat   (float32 LE)
 *   [17..20] drone_lon   (float32 LE)
 *   [21..24] drone_alt   (float32 LE)
 *   [25..28] drone_speed (float32 LE)
 *   [29..32] operator_lat (float32 LE)
 *   [33..36] operator_lon (float32 LE)
 */
static bool parse_payload(const uint8_t *data, uint16_t len,
                          remote_id_data_t *out)
{
    if (len < REMOTE_ID_MIN_LEN) {
        return false;
    }
    if (data[0] != REMOTE_ID_MARKER) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    /* Serial: 12 ASCII bytes, null-terminated */
    memcpy(out->serial, &data[1], 12);
    out->serial[12] = '\0';

    /* GPS fields */
    out->drone_lat    = read_float32_le(&data[13]);
    out->drone_lon    = read_float32_le(&data[17]);
    out->drone_alt    = read_float32_le(&data[21]);
    out->drone_speed  = read_float32_le(&data[25]);
    out->operator_lat = read_float32_le(&data[29]);
    out->operator_lon = read_float32_le(&data[33]);

    out->emergency_status = 0;
    out->valid = true;
    return true;
}

/* ── Public API ──────────────────────────────────────────────────── */

bool remote_id_parse_ble(const uint8_t *service_data, uint16_t len,
                         remote_id_data_t *out)
{
    if (!service_data || !out || len < REMOTE_ID_MIN_LEN) {
        return false;
    }

    return parse_payload(service_data, len, out);
}

bool remote_id_parse_wifi_nan(const uint8_t *frame, uint16_t len,
                              remote_id_data_t *out)
{
    if (!frame || !out || len < REMOTE_ID_MIN_LEN) {
        return false;
    }

    /*
     * WiFi NAN action frames carry the Open Drone ID payload within
     * the NAN service data. Scan through the frame looking for the
     * 0x0F marker byte that starts the Remote ID payload.
     */
    for (uint16_t i = 0; i <= len - REMOTE_ID_MIN_LEN; i++) {
        if (frame[i] == REMOTE_ID_MARKER) {
            if (parse_payload(&frame[i], len - i, out)) {
                return true;
            }
        }
    }

    return false;
}
