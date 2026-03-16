/**
 * WiFi promiscuous mode scanner — captures probe requests.
 *
 * Runs on Core 0. The promiscuous callback copies minimal data (MAC + SSID +
 * RSSI + timestamp) into a FreeRTOS queue. The analysis task on Core 1
 * dequeues and processes.
 *
 * Channel hopping: weighted dwell (200ms on 1/6/11, 100ms on others).
 */
#pragma once

#include <stdint.h>

/** Packet info extracted in the promiscuous callback (SRAM only). */
typedef struct {
    uint8_t  src_mac[6];
    char     ssid[33];      /* Null-terminated, max 32 chars */
    uint8_t  ssid_len;
    int8_t   rssi;
    uint8_t  channel;
    uint32_t timestamp;     /* Epoch seconds from GPS or uptime */
} wifi_packet_info_t;

/** Start WiFi promiscuous scanning (creates channel hop task on Core 0). */
void wifi_scanner_start(void);

/** Stop WiFi promiscuous scanning. */
void wifi_scanner_stop(void);

/** Returns the FreeRTOS queue handle for dequeuing wifi_packet_info_t items. */
void *wifi_scanner_get_queue(void);
