/**
 * GPS NMEA parser — reads $GPRMC and $GPGGA from UART1.
 *
 * Runs a background FreeRTOS task that accumulates bytes into a line
 * buffer, parses complete sentences, and updates a mutex-protected
 * gps_fix_t struct for thread-safe reads by other modules.
 */

#include "gps_parser.h"
#include "../cyt_config.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static const char *TAG = "gps";

/* ── Module state ──────────────────────────────────────────────────── */

static gps_fix_t    s_fix;
static SemaphoreHandle_t s_mutex = NULL;
static bool         s_rtc_set = false;

/* ── NMEA line buffer ──────────────────────────────────────────────── */

#define NMEA_MAX_LEN 128
#define GPS_UART_BUF_SIZE 512
#define GPS_TASK_STACK_SIZE 3072

/* ── Helper: split NMEA field ──────────────────────────────────────── */

/**
 * Advance *p past the next comma and return pointer to the start of
 * the field.  Returns empty string if at end.
 */
static const char *next_field(const char **p)
{
    const char *start = *p;
    while (**p && **p != ',') {
        (*p)++;
    }
    if (**p == ',') {
        (*p)++;
    }
    return start;
}

/**
 * Parse a float from an NMEA field (up to next comma or end).
 * Returns 0.0 if the field is empty.
 */
static float field_to_float(const char *field)
{
    if (*field == '\0' || *field == ',') {
        return 0.0f;
    }
    return strtof(field, NULL);
}

/* ── NMEA coordinate conversion ────────────────────────────────────── */

/**
 * Convert NMEA lat/lon format (dddmm.mmm) to decimal degrees.
 *   latitude:  ddmm.mmm  -> 2-digit degree
 *   longitude: dddmm.mmm -> 3-digit degree
 */
static float nmea_to_decimal(float raw, char hemisphere, bool is_lon)
{
    if (raw == 0.0f) {
        return 0.0f;
    }

    float divisor = is_lon ? 100.0f : 100.0f;
    int degrees = (int)(raw / divisor);
    float minutes = raw - (degrees * divisor);
    float decimal = degrees + (minutes / 60.0f);

    if (hemisphere == 'S' || hemisphere == 'W') {
        decimal = -decimal;
    }

    return decimal;
}

/* ── Time parsing ──────────────────────────────────────────────────── */

/**
 * Parse NMEA time (hhmmss.ss) and date (ddmmyy) into Unix epoch.
 * Returns 0 if either field is empty.
 */
static uint32_t nmea_datetime_to_epoch(const char *time_str, const char *date_str)
{
    if (!time_str || !date_str ||
        *time_str == ',' || *time_str == '\0' ||
        *date_str == ',' || *date_str == '\0') {
        return 0;
    }

    if (strlen(time_str) < 6 || strlen(date_str) < 6) {
        return 0;
    }

    struct tm t = {0};

    /* hhmmss.ss */
    t.tm_hour = (time_str[0] - '0') * 10 + (time_str[1] - '0');
    t.tm_min  = (time_str[2] - '0') * 10 + (time_str[3] - '0');
    t.tm_sec  = (time_str[4] - '0') * 10 + (time_str[5] - '0');

    /* ddmmyy */
    t.tm_mday = (date_str[0] - '0') * 10 + (date_str[1] - '0');
    t.tm_mon  = (date_str[2] - '0') * 10 + (date_str[3] - '0') - 1;
    int year  = (date_str[4] - '0') * 10 + (date_str[5] - '0');
    t.tm_year = year + 100;  /* Years since 1900; 2000+yy */

    /* mktime expects local time but we have UTC — use timegm if available,
     * otherwise set timezone and use mktime. ESP-IDF provides mktime with
     * TZ=UTC by default. */
    time_t epoch = mktime(&t);
    return (epoch > 0) ? (uint32_t)epoch : 0;
}

/* ── NMEA checksum validation ──────────────────────────────────────── */

/**
 * Validate NMEA checksum: XOR all bytes between '$' and '*'.
 */
static bool nmea_checksum_valid(const char *sentence)
{
    if (sentence[0] != '$') {
        return false;
    }

    uint8_t checksum = 0;
    const char *p = sentence + 1;  /* Skip '$' */

    while (*p && *p != '*') {
        checksum ^= (uint8_t)*p;
        p++;
    }

    if (*p != '*') {
        return false;  /* No checksum delimiter */
    }

    /* Parse expected checksum (2 hex digits after '*') */
    unsigned int expected = 0;
    if (sscanf(p + 1, "%2x", &expected) != 1) {
        return false;
    }

    return checksum == (uint8_t)expected;
}

/* ── Sentence parsers ──────────────────────────────────────────────── */

/**
 * Parse $GPRMC sentence.
 * Format: $GPRMC,hhmmss.ss,A,llll.ll,N,yyyyy.yy,W,speed,heading,ddmmyy,...
 */
static void parse_gprmc(const char *sentence)
{
    /* Skip "$GPRMC," */
    const char *p = sentence + 7;

    /* Field 1: time */
    const char *time_str = next_field(&p);

    /* Field 2: status (A=active/valid, V=void) */
    const char *status = next_field(&p);
    bool valid = (*status == 'A');

    /* Field 3: latitude */
    const char *lat_str = next_field(&p);
    float lat_raw = field_to_float(lat_str);

    /* Field 4: N/S */
    const char *lat_dir = next_field(&p);

    /* Field 5: longitude */
    const char *lon_str = next_field(&p);
    float lon_raw = field_to_float(lon_str);

    /* Field 6: E/W */
    const char *lon_dir = next_field(&p);

    /* Field 7: speed in knots */
    const char *speed_str = next_field(&p);

    /* Field 8: heading (skip) */
    next_field(&p);

    /* Field 9: date */
    const char *date_str = next_field(&p);

    /* Need to null-terminate fields at commas for parsing */
    /* (field_to_float and nmea_datetime_to_epoch handle comma-terminated strings) */

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_fix.has_fix = valid;

    if (valid) {
        s_fix.latitude = nmea_to_decimal(lat_raw, *lat_dir, false);
        s_fix.longitude = nmea_to_decimal(lon_raw, *lon_dir, true);
        s_fix.speed_knots = field_to_float(speed_str);

        uint32_t epoch = nmea_datetime_to_epoch(time_str, date_str);
        if (epoch > 0) {
            s_fix.utc_timestamp = epoch;

            /* Set RTC on first valid fix */
            if (!s_rtc_set) {
                struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
                settimeofday(&tv, NULL);
                s_rtc_set = true;
                ESP_LOGI(TAG, "RTC set from GPS: epoch %u", (unsigned)epoch);
            }
        }
    }

    xSemaphoreGive(s_mutex);

    if (valid) {
        ESP_LOGD(TAG, "RMC fix: %.6f, %.6f  speed=%.1f kn",
                 s_fix.latitude, s_fix.longitude, s_fix.speed_knots);
    }
}

/**
 * Parse $GPGGA sentence.
 * Format: $GPGGA,hhmmss.ss,llll.ll,N,yyyyy.yy,W,quality,sats,hdop,alt,M,...
 */
static void parse_gpgga(const char *sentence)
{
    /* Skip "$GPGGA," */
    const char *p = sentence + 7;

    /* Field 1: time (skip, use RMC for time) */
    next_field(&p);

    /* Field 2: latitude */
    const char *lat_str = next_field(&p);
    float lat_raw = field_to_float(lat_str);

    /* Field 3: N/S */
    const char *lat_dir = next_field(&p);

    /* Field 4: longitude */
    const char *lon_str = next_field(&p);
    float lon_raw = field_to_float(lon_str);

    /* Field 5: E/W */
    const char *lon_dir = next_field(&p);

    /* Field 6: fix quality (0=invalid, 1=GPS, 2=DGPS) */
    const char *quality_str = next_field(&p);
    int quality = (*quality_str >= '0' && *quality_str <= '9') ?
                  (*quality_str - '0') : 0;

    /* Field 7: satellites */
    const char *sats_str = next_field(&p);

    /* Field 8: HDOP (skip) */
    next_field(&p);

    /* Field 9: altitude */
    const char *alt_str = next_field(&p);

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (quality > 0) {
        s_fix.satellites = (uint8_t)atoi(sats_str);
        s_fix.altitude_m = field_to_float(alt_str);

        /* Also update lat/lon from GGA if available */
        if (lat_raw != 0.0f) {
            s_fix.latitude = nmea_to_decimal(lat_raw, *lat_dir, false);
            s_fix.longitude = nmea_to_decimal(lon_raw, *lon_dir, true);
        }
    }

    xSemaphoreGive(s_mutex);

    ESP_LOGD(TAG, "GGA: quality=%d sats=%s alt=%s",
             quality, sats_str, alt_str);
}

/* ── Line dispatch ─────────────────────────────────────────────────── */

static void process_nmea_line(const char *line)
{
    if (strlen(line) < 7) {
        return;
    }

    /* Validate checksum if present */
    if (strchr(line, '*')) {
        if (!nmea_checksum_valid(line)) {
            ESP_LOGD(TAG, "Checksum failed: %.20s...", line);
            return;
        }
    }

    if (strncmp(line, "$GPRMC,", 7) == 0) {
        parse_gprmc(line);
    } else if (strncmp(line, "$GPGGA,", 7) == 0) {
        parse_gpgga(line);
    }
    /* Other sentence types are silently ignored */
}

/* ── UART reader task ──────────────────────────────────────────────── */

static void gps_task(void *arg)
{
    char line_buf[NMEA_MAX_LEN];
    int line_pos = 0;
    uint8_t byte_buf[64];

    ESP_LOGI(TAG, "GPS reader task started on UART%d", CYT_GPS_UART_NUM);

    for (;;) {
        int len = uart_read_bytes(CYT_GPS_UART_NUM, byte_buf, sizeof(byte_buf),
                                  pdMS_TO_TICKS(200));
        if (len <= 0) {
            continue;
        }

        for (int i = 0; i < len; i++) {
            char c = (char)byte_buf[i];

            if (c == '\n' || c == '\r') {
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';
                    process_nmea_line(line_buf);
                    line_pos = 0;
                }
                continue;
            }

            if (c == '$') {
                /* Start of new sentence — reset buffer */
                line_pos = 0;
            }

            if (line_pos < NMEA_MAX_LEN - 1) {
                line_buf[line_pos++] = c;
            } else {
                /* Line too long — discard and wait for next '$' */
                line_pos = 0;
            }
        }
    }
}

/* ── Public API ────────────────────────────────────────────────────── */

int gps_init(void)
{
    /* Create mutex */
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create GPS mutex");
        return -1;
    }

    /* Initialize fix struct */
    memset(&s_fix, 0, sizeof(s_fix));

    /* Configure UART */
    const uart_config_t uart_config = {
        .baud_rate  = CYT_GPS_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(CYT_GPS_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(err));
        return -1;
    }

    err = uart_set_pin(CYT_GPS_UART_NUM,
                       CYT_GPS_TX_PIN, CYT_GPS_RX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(err));
        return -1;
    }

    err = uart_driver_install(CYT_GPS_UART_NUM, GPS_UART_BUF_SIZE,
                              0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(err));
        return -1;
    }

    /* Start background reader task */
    BaseType_t ret = xTaskCreate(gps_task, "gps_rx", GPS_TASK_STACK_SIZE,
                                 NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GPS task");
        return -1;
    }

    ESP_LOGI(TAG, "GPS initialized: UART%d @ %d baud (TX=%d, RX=%d)",
             CYT_GPS_UART_NUM, CYT_GPS_BAUD_RATE,
             CYT_GPS_TX_PIN, CYT_GPS_RX_PIN);

    return 0;
}

gps_fix_t gps_get_fix(void)
{
    gps_fix_t copy;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(&copy, &s_fix, sizeof(copy));
    xSemaphoreGive(s_mutex);
    return copy;
}

bool gps_has_fix(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool fix = s_fix.has_fix;
    xSemaphoreGive(s_mutex);
    return fix;
}

uint32_t gps_get_timestamp(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool has = s_fix.has_fix;
    uint32_t ts = s_fix.utc_timestamp;
    xSemaphoreGive(s_mutex);

    if (has && ts > 0) {
        return ts;
    }

    /* Fallback: uptime in seconds */
    return (uint32_t)(esp_timer_get_time() / 1000000);
}
