/**
 * CYT-NG Handheld — Main entry point.
 *
 * Architecture:
 *   Core 0: WiFi promiscuous callback, channel hopping, BLE scanning
 *   Core 1: Analysis task, display task, SD logger task, GPS task
 *
 * Scan window cycle:
 *   [WiFi promiscuous: 45s] → [BLE GAP scan: 10s] → repeat
 *   CC1101 sub-GHz: continuous (separate SPI hardware, no radio conflict)
 *   GPS: sampled every 30s on UART (independent)
 *
 * Data flow:
 *   Promiscuous callback (Core 0) → packet queue (SRAM) → analysis task (Core 1)
 *     → device table (PSRAM) → display update + SD logger
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "cyt_config.h"
#include "scanners/wifi_scanner.h"
#include "scanners/ble_scanner.h"
#include "scanners/cc1101_scanner.h"
#include "storage/device_table.h"
#include "storage/sd_logger.h"
#include "gps/gps_parser.h"
#include "ui/display.h"

static const char *TAG = "cyt_main";

/* ── Forward declarations ─────────────────────────────────────────── */
static void analysis_task(void *arg);
static void display_task(void *arg);
static void logger_task(void *arg);
static void scan_window_task(void *arg);

void app_main(void)
{
    ESP_LOGI(TAG, "CYT-NG Handheld starting...");

    /* ── NVS init (required for WiFi) ──────────────────────────── */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ── Initialize subsystems ─────────────────────────────────── */
    if (device_table_init() != 0) {
        ESP_LOGE(TAG, "Failed to initialize device table");
        return;
    }
    ESP_LOGI(TAG, "Device table initialized (%d max devices)", CYT_MAX_DEVICES);

    gps_init();
    ESP_LOGI(TAG, "GPS initialized on UART%d", CYT_GPS_UART_NUM);

    display_init();
    ESP_LOGI(TAG, "Display initialized (%dx%d)", CYT_DISPLAY_WIDTH, CYT_DISPLAY_HEIGHT);

    if (sd_logger_init() == 0) {
        ESP_LOGI(TAG, "SD card mounted at %s", CYT_SD_MOUNT_POINT);
        sd_logger_start_session();
    } else {
        ESP_LOGW(TAG, "SD card not available — logging disabled");
    }

    if (cc1101_scanner_init() == 0) {
        ESP_LOGI(TAG, "CC1101 initialized — TPMS scanning active");
        cc1101_scanner_start();
    } else {
        ESP_LOGW(TAG, "CC1101 not available — TPMS scanning disabled");
    }

    /* ── Create tasks ──────────────────────────────────────────── */

    /* Core 0: scan window management (WiFi/BLE alternation) */
    xTaskCreatePinnedToCore(scan_window_task, "scan_window", 4096,
                            NULL, 5, NULL, 0);

    /* Core 1: analysis (dequeue packets, update device table) */
    xTaskCreatePinnedToCore(analysis_task, "analysis", 8192,
                            NULL, 4, NULL, 1);

    /* Core 1: display refresh */
    xTaskCreatePinnedToCore(display_task, "display", 4096,
                            NULL, 2, NULL, 1);

    /* Core 1: SD card batch writer */
    xTaskCreatePinnedToCore(logger_task, "logger", 4096,
                            NULL, 1, NULL, 1);

    ESP_LOGI(TAG, "All tasks started. Scanning...");
}

/* ── Scan window task (Core 0) ───────────────────────────────────── */

static void scan_window_task(void *arg)
{
    for (;;) {
        /* WiFi promiscuous window */
        ESP_LOGI(TAG, "WiFi scan window (%d ms)", CYT_WIFI_SCAN_WINDOW_MS);
        wifi_scanner_start();
        vTaskDelay(pdMS_TO_TICKS(CYT_WIFI_SCAN_WINDOW_MS));
        wifi_scanner_stop();

        /* BLE scan window */
        ESP_LOGI(TAG, "BLE scan window (%d ms)", CYT_BLE_SCAN_WINDOW_MS);
        ble_scanner_start();
        vTaskDelay(pdMS_TO_TICKS(CYT_BLE_SCAN_WINDOW_MS));
        ble_scanner_stop();
    }
}

/* ── Analysis task (Core 1) ──────────────────────────────────────── */

static void analysis_task(void *arg)
{
    QueueHandle_t wifi_q   = wifi_scanner_get_queue();
    QueueHandle_t ble_q    = ble_scanner_get_queue();
    QueueHandle_t cc1101_q = cc1101_scanner_get_queue();

    wifi_packet_info_t  wifi_pkt;
    ble_detection_t     ble_det;
    cc1101_detection_t  cc1101_det;

    TickType_t last_rotation = xTaskGetTickCount();

    for (;;) {
        /* Drain WiFi queue */
        while (xQueueReceive(wifi_q, &wifi_pkt, 0) == pdTRUE) {
            device_record_t *rec = device_table_upsert(
                wifi_pkt.src_mac, SOURCE_WIFI);
            if (rec) {
                memcpy(rec->ssid, wifi_pkt.ssid, wifi_pkt.ssid_len + 1);
                rec->ssid_len = wifi_pkt.ssid_len;
                rec->rssi_avg = wifi_pkt.rssi;
                rec->last_seen = wifi_pkt.timestamp;
                rec->window_flags |= 0x01; /* Current 5-min window */
            }
        }

        /* Drain BLE queue */
        while (xQueueReceive(ble_q, &ble_det, 0) == pdTRUE) {
            device_record_t *rec = device_table_upsert(
                ble_det.device_id, SOURCE_BLE);
            if (rec) {
                rec->rssi_avg = ble_det.rssi;
                rec->last_seen = gps_get_timestamp();
                rec->window_flags |= 0x01;
            }
        }

        /* Drain CC1101 queue */
        while (xQueueReceive(cc1101_q, &cc1101_det, 0) == pdTRUE) {
            device_record_t *rec = device_table_upsert(
                cc1101_det.device_id, SOURCE_TPMS);
            if (rec) {
                rec->rssi_avg = cc1101_det.rssi;
                rec->last_seen = cc1101_det.timestamp;
                rec->window_flags |= 0x01;
            }
        }

        /* Rotate time windows every 5 minutes */
        if ((xTaskGetTickCount() - last_rotation) >=
            pdMS_TO_TICKS(CYT_WINDOW_ROTATION_INTERVAL_S * 1000)) {
            device_table_rotate_windows();
            last_rotation = xTaskGetTickCount();
            ESP_LOGI(TAG, "Windows rotated. Active: %lu, Suspicious: %lu",
                     (unsigned long)device_table_active_count(),
                     (unsigned long)device_table_suspicious_count());
        }

        vTaskDelay(pdMS_TO_TICKS(100)); /* 10 Hz analysis rate */
    }
}

/* ── Display task (Core 1) ───────────────────────────────────────── */

static void display_task(void *arg)
{
    for (;;) {
        display_status_t status = {
            .total_devices = device_table_active_count(),
            .suspicious_count = device_table_suspicious_count(),
            .gps_fix = gps_has_fix(),
            .sd_ready = sd_logger_is_ready(),
            .session_active = true,
        };
        /* TODO: populate per-source counts, highest persistence, battery */

        display_update(&status);
        vTaskDelay(pdMS_TO_TICKS(CYT_DISPLAY_UPDATE_MS));
    }
}

/* ── Logger task (Core 1) ────────────────────────────────────────── */

static void logger_task(void *arg)
{
    for (;;) {
        if (sd_logger_is_ready()) {
            sd_logger_flush();
        }
        vTaskDelay(pdMS_TO_TICKS(CYT_SD_LOG_INTERVAL_S * 1000));
    }
}
