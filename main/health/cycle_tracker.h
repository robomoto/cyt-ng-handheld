/**
 * Cycle/fertility tracker — real menstrual cycle tracking.
 *
 * Calendar-based tracker. User logs cycle start dates via buttons or
 * phone companion. Device calculates:
 *   - Current cycle day
 *   - Predicted fertile window (days 10-17 of a 28-day cycle, adjusted
 *     to the user's actual average cycle length)
 *   - Predicted next period start
 *   - Average cycle length (rolling average of last 6 cycles)
 *   - Basal body temperature logging (manual entry)
 *
 * All data stored in NVS. Encrypted when stealth mode is active.
 * This is sensitive health data — handled accordingly.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/** Maximum number of cycle start dates stored. */
#define CYCLE_MAX_ENTRIES   24      /* 2 years of history */

/** Cycle tracker state. */
typedef struct {
    uint8_t  current_day;           /* Day N of current cycle (1-based) */
    uint8_t  avg_cycle_length;      /* Rolling average (days) */
    uint8_t  fertile_window_start;  /* Day N (typically 10) */
    uint8_t  fertile_window_end;    /* Day N (typically 17) */
    bool     in_fertile_window;
    uint32_t next_period_date;      /* Predicted epoch timestamp */
    uint32_t last_period_date;      /* Most recent logged start */
    uint8_t  cycle_count;           /* Number of logged cycles */
    float    bbt_today;             /* Basal body temp (°F), 0 if not logged */
} cycle_status_t;

/** Initialize cycle tracker (loads history from NVS). */
int cycle_tracker_init(void);

/** Log a new cycle start date (today). Called by user via button or phone. */
void cycle_tracker_log_period_start(void);

/** Log a cycle start on a specific date (epoch timestamp). From phone app. */
void cycle_tracker_log_period_start_on(uint32_t epoch_date);

/** Log today's basal body temperature (°F). */
void cycle_tracker_log_bbt(float temp_f);

/** Get current cycle status. Thread-safe. */
cycle_status_t cycle_tracker_get_status(void);

/** Check if tracker has enough data to make predictions (≥2 cycles). */
bool cycle_tracker_has_data(void);
