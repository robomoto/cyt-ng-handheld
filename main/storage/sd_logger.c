/**
 * SD card session logger — FATFS over SPI, CSV output for base station import.
 *
 * Uses the shared SPI2 bus (MOSI=11, MISO=13, CLK=12) with its own CS (GPIO 10).
 * Writes are buffered in a 64KB PSRAM buffer and flushed periodically.
 */

#include "storage/sd_logger.h"
#include "cyt_config.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/spi_master.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

static const char *TAG = "sd_logger";

/* ── Module state ───────────────────────────────────────────────── */

static sdmmc_card_t  *s_card;
static FILE           *s_file;
static char            s_session_path[80];
static bool            s_mounted;
static SemaphoreHandle_t s_mutex;

/* 64KB write buffer in PSRAM */
#define SD_BUFFER_SIZE  65536
static char           *s_buffer;
static size_t          s_buffer_pos;

/* Session metadata */
static uint32_t        s_session_start;
static uint32_t        s_record_count;

/* Shared SPI bus flag — defined in cc1101_scanner.c */
extern bool g_spi_bus_initialized;

/* ── CSV format ─────────────────────────────────────────────────── */

static const char *CSV_HEADER =
    "timestamp,mac,device_id,source_type,rssi,lat,lon,ssid,window_flags,appearance_count\n";

static const char *source_type_str(uint8_t src)
{
    switch (src) {
    case SOURCE_WIFI:  return "wifi";
    case SOURCE_BLE:   return "ble";
    case SOURCE_TPMS:  return "tpms";
    case SOURCE_DRONE: return "drone";
    default:           return "unknown";
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

int sd_logger_init(void)
{
    ESP_LOGI(TAG, "Initializing SD card logger");

    /* Initialize SPI bus if not already done (CC1101 may have done it) */
    if (!g_spi_bus_initialized) {
        spi_bus_config_t bus_cfg = {
            .mosi_io_num     = CYT_SPI_MOSI_PIN,
            .miso_io_num     = CYT_SPI_MISO_PIN,
            .sclk_io_num     = CYT_SPI_CLK_PIN,
            .quadwp_io_num   = -1,
            .quadhd_io_num   = -1,
            .max_transfer_sz = 4096,
        };

        esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
            return -1;
        }
        g_spi_bus_initialized = true;
        ESP_LOGI(TAG, "SPI2 bus initialized");
    }

    /* Mount FAT filesystem on SD card via SPI */
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs   = CYT_SD_CS_PIN;
    slot_cfg.host_id   = SPI2_HOST;

    esp_err_t err = esp_vfs_fat_sdspi_mount(CYT_SD_MOUNT_POINT, &host,
                                             &slot_cfg, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(err));
        s_mounted = false;
        return -1;
    }

    /* Log card info */
    sdmmc_card_print_info(stdout, s_card);
    s_mounted = true;

    /* Allocate PSRAM write buffer */
    s_buffer = heap_caps_malloc(SD_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_buffer) {
        ESP_LOGW(TAG, "PSRAM alloc failed, falling back to internal RAM");
        s_buffer = malloc(SD_BUFFER_SIZE);
        if (!s_buffer) {
            ESP_LOGE(TAG, "Buffer allocation failed entirely");
            return -1;
        }
    }
    s_buffer_pos = 0;

    /* Mutex for thread-safe buffer access */
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return -1;
    }

    ESP_LOGI(TAG, "SD card logger initialized, mount=%s", CYT_SD_MOUNT_POINT);
    return 0;
}

int sd_logger_start_session(void)
{
    if (!s_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return -1;
    }

    if (s_file) {
        ESP_LOGW(TAG, "Session already active, ending previous session");
        sd_logger_end_session();
    }

    /* Generate filename from current time */
    time_t now;
    time(&now);
    struct tm tm_info;
    localtime_r(&now, &tm_info);

    snprintf(s_session_path, sizeof(s_session_path),
             "%s/cyt_session_%04d%02d%02d_%02d%02d%02d.csv",
             CYT_SD_MOUNT_POINT,
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);

    s_file = fopen(s_session_path, "w");
    if (!s_file) {
        ESP_LOGE(TAG, "Failed to create session file: %s", s_session_path);
        return -1;
    }

    s_session_start = (uint32_t)now;
    s_record_count = 0;

    /* Write comment header with session metadata */
    fprintf(s_file, "# session_id=%08lX,fw_ver=1.0.0,start=%lu,end=0,device_count=0\n",
            (unsigned long)(s_session_start ^ 0xDEADBEEF),
            (unsigned long)s_session_start);

    /* Write CSV column header */
    fputs(CSV_HEADER, s_file);
    fflush(s_file);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_buffer_pos = 0;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Session started: %s", s_session_path);
    return 0;
}

int sd_logger_end_session(void)
{
    if (!s_file) {
        ESP_LOGW(TAG, "No active session to end");
        return -1;
    }

    /* Flush remaining buffer */
    sd_logger_flush();

    /* Write end metadata as a trailing comment */
    time_t now;
    time(&now);
    fprintf(s_file, "# end=%lu,device_count=%lu\n",
            (unsigned long)now, (unsigned long)s_record_count);

    fclose(s_file);
    s_file = NULL;

    /*
     * Rewrite the first-line metadata with correct end time and count.
     * We do this by reading the file, patching the header, rewriting.
     * For simplicity in v1, just append — the base station importer
     * should handle trailing metadata comments.
     */

    ESP_LOGI(TAG, "Session ended: %lu records written to %s",
             (unsigned long)s_record_count, s_session_path);
    return 0;
}

void sd_logger_record(const device_record_t *device, const gps_fix_t *gps,
                      const char *device_id_str)
{
    if (!s_file || !s_buffer) {
        return;
    }

    /* Format the MAC/device_id as hex string */
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             device->device_id[0], device->device_id[1],
             device->device_id[2], device->device_id[3],
             device->device_id[4], device->device_id[5]);

    /* Format SSID — escape commas for CSV safety */
    char ssid_safe[68];
    if (device->ssid_len > 0) {
        /* Wrap in quotes if SSID contains commas or quotes */
        bool needs_quote = false;
        for (int i = 0; i < device->ssid_len; i++) {
            if (device->ssid[i] == ',' || device->ssid[i] == '"') {
                needs_quote = true;
                break;
            }
        }
        if (needs_quote) {
            snprintf(ssid_safe, sizeof(ssid_safe), "\"%.*s\"",
                     device->ssid_len, device->ssid);
        } else {
            snprintf(ssid_safe, sizeof(ssid_safe), "%.*s",
                     device->ssid_len, device->ssid);
        }
    } else {
        ssid_safe[0] = '\0';
    }

    /* Format the CSV line */
    char line[256];
    int len = snprintf(line, sizeof(line),
                       "%lu,%s,%s,%s,%d,%.6f,%.6f,%s,0x%02X,%u\n",
                       (unsigned long)device->last_seen,
                       mac_str,
                       device_id_str ? device_id_str : mac_str,
                       source_type_str(device->source_type),
                       device->rssi_avg,
                       gps ? gps->latitude : 0.0f,
                       gps ? gps->longitude : 0.0f,
                       ssid_safe,
                       device->window_flags,
                       device->appearance_count);

    if (len <= 0 || (size_t)len >= sizeof(line)) {
        ESP_LOGW(TAG, "CSV line formatting error");
        return;
    }

    /* Append to buffer (thread-safe) */
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_buffer_pos + (size_t)len >= SD_BUFFER_SIZE) {
        /* Buffer full — need to flush first */
        xSemaphoreGive(s_mutex);
        sd_logger_flush();
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }

    memcpy(s_buffer + s_buffer_pos, line, (size_t)len);
    s_buffer_pos += (size_t)len;
    s_record_count++;

    xSemaphoreGive(s_mutex);
}

void sd_logger_flush(void)
{
    if (!s_file || !s_buffer) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_buffer_pos == 0) {
        xSemaphoreGive(s_mutex);
        return;
    }

    size_t to_write = s_buffer_pos;

    /* Write buffer contents to SD card */
    size_t written = fwrite(s_buffer, 1, to_write, s_file);
    if (written != to_write) {
        ESP_LOGE(TAG, "SD write error: wrote %u of %u bytes",
                 (unsigned)written, (unsigned)to_write);
    }

    fflush(s_file);
    s_buffer_pos = 0;

    xSemaphoreGive(s_mutex);

    ESP_LOGD(TAG, "Flushed %u bytes to SD", (unsigned)to_write);
}

const char *sd_logger_get_session_path(void)
{
    return s_session_path;
}

bool sd_logger_is_ready(void)
{
    return s_mounted && (s_file != NULL);
}
