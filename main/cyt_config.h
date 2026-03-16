/**
 * CYT-NG Handheld — Pin assignments and configuration constants.
 *
 * Target: LilyGo T-Display-S3 (non-touch, 8MB PSRAM)
 */
#pragma once

/* ── WiFi / BLE scan windows ─────────────────────────────────────── */
#define CYT_WIFI_SCAN_WINDOW_MS     45000   /* 45 seconds WiFi promiscuous */
#define CYT_BLE_SCAN_WINDOW_MS      10000   /* 10 seconds BLE scanning */
#define CYT_CC1101_LISTEN_CONTINUOUS 1       /* CC1101 listens in background */

/* ── WiFi channel hopping ────────────────────────────────────────── */
#define CYT_CHANNEL_DWELL_PRIMARY_MS  200   /* Channels 1, 6, 11 */
#define CYT_CHANNEL_DWELL_SECONDARY_MS 100  /* All other channels */

/* ── Device tracking ─────────────────────────────────────────────── */
#define CYT_MAX_DEVICES             10000   /* Max tracked devices (in PSRAM) */
#define CYT_HASH_BUCKETS            8192    /* Hash table buckets (in SRAM) */
#define CYT_WINDOW_ROTATION_INTERVAL_S 300  /* 5 minutes per time window */
#define CYT_PACKET_QUEUE_SIZE       512     /* Ring buffer for packet callbacks */

/* ── GPS (UART1 — u-blox NEO-M8N) ───────────────────────────────── */
#define CYT_GPS_UART_NUM            1
#define CYT_GPS_TX_PIN              43
#define CYT_GPS_RX_PIN              44
#define CYT_GPS_BAUD_RATE           9600
#define CYT_GPS_POLL_INTERVAL_MS    30000   /* Read GPS every 30 seconds */

/* ── SD Card (SPI — shared bus with CC1101) ──────────────────────── */
#define CYT_SD_CS_PIN               10
#define CYT_SPI_MOSI_PIN            11
#define CYT_SPI_CLK_PIN             12
#define CYT_SPI_MISO_PIN            13
#define CYT_SD_MOUNT_POINT          "/sdcard"
#define CYT_SD_LOG_INTERVAL_S       60      /* Batch write to SD every 60s */

/* ── CC1101 sub-GHz (SPI — shared bus with SD) ──────────────────── */
/*
 * Pins 6, 7, 9 are used by the AMOLED QSPI bus on the T-Display-S3
 * AMOLED variant — CC1101 moved to free GPIOs.  CC1101 hardware reset
 * removed: use the SPI SRES strobe (command 0x30) instead.
 */
#define CYT_CC1101_CS_PIN           15
#define CYT_CC1101_GDO0_PIN         16      /* IRQ — packet received */
#define CYT_CC1101_GDO2_PIN         21      /* Optional status */

/* ── Buzzer (PWM via LEDC) ──────────────────────────────────────── */
#define CYT_BUZZER_PIN              1
#define CYT_BUZZER_FREQ_HZ          2700
#define CYT_BUZZER_ALERT_DURATION_MS 50     /* Short, discreet */

/* ── Buttons ────────────────────────────────────────────────────── */
#define CYT_BTN_UP_PIN              2
#define CYT_BTN_DOWN_PIN            3
#define CYT_BTN_MODE_PIN            14

/* ── Display (RM67162 AMOLED — T-Display-S3 AMOLED, QSPI interface) */
#define CYT_DISPLAY_WIDTH           240
#define CYT_DISPLAY_HEIGHT          536
#define CYT_DISPLAY_UPDATE_MS       2000    /* Refresh every 2 seconds */
#define CYT_DISPLAY_AUTO_OFF_MS     30000   /* Dim after 30 seconds */

/* ── Alert thresholds ───────────────────────────────────────────── */
#define CYT_PERSISTENCE_ALERT_THRESHOLD 0.7f
#define CYT_MULTI_WINDOW_ALERT_BITS    0x03  /* Device in 2+ time windows */
