/**
 * SD card session logger — writes CSV data for base station import.
 *
 * CSV format matches the base station's HandheldImporter:
 *   # session_id=<uuid>,fw_ver=1.0.0,start=<epoch>,end=<epoch>,device_count=N
 *   timestamp,mac,device_id,source_type,rssi,lat,lon,ssid,window_flags,appearance_count
 *
 * Writes are batched in a 64KB PSRAM buffer and flushed every 60 seconds
 * to minimize SD card wear and SPI bus contention with CC1101.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "storage/device_table.h"
#include "gps/gps_parser.h"

/** Initialize SD card (SPI mount). Returns 0 on success. */
int sd_logger_init(void);

/** Start a new session. Creates a new CSV file on the SD card. */
int sd_logger_start_session(void);

/** End the current session. Writes final metadata and closes the file. */
int sd_logger_end_session(void);

/** Log a device record with current GPS fix. Buffered — not written immediately. */
void sd_logger_record(const device_record_t *device, const gps_fix_t *gps,
                      const char *device_id_str);

/** Flush the write buffer to SD card. Called every CYT_SD_LOG_INTERVAL_S. */
void sd_logger_flush(void);

/** Get the current session file path (for USB upload). */
const char *sd_logger_get_session_path(void);

/** Check if SD card is mounted and writable. */
bool sd_logger_is_ready(void);
