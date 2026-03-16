/**
 * Mental health mood tracker — daily mood logging with trends.
 *
 * User logs mood on a 1-10 scale using the 3 buttons (UP/DOWN to
 * adjust, MODE to confirm). Stored with timestamp. Device shows:
 *   - Current logged mood
 *   - 7-day trend (improving / stable / declining)
 *   - Days since last log (reminder if >24h)
 *   - Simple text prompts ("How are you feeling?")
 *
 * Designed to be genuinely useful, not just a disguise.
 * Data stored in NVS. Sensitive — encrypted in stealth mode.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/** Maximum mood entries stored. */
#define MOOD_MAX_ENTRIES    90      /* ~3 months daily */

/** Mood trend direction. */
typedef enum {
    MOOD_TREND_UNKNOWN = 0,
    MOOD_TREND_IMPROVING,
    MOOD_TREND_STABLE,
    MOOD_TREND_DECLINING,
} mood_trend_t;

/** Mood tracker status. */
typedef struct {
    uint8_t     last_mood;          /* 1-10, or 0 if never logged */
    uint32_t    last_log_time;      /* Epoch timestamp */
    float       avg_7day;           /* Rolling 7-day average */
    mood_trend_t trend;             /* 7-day trend direction */
    uint16_t    total_entries;
    uint16_t    hours_since_last;   /* Hours since last log */
    bool        needs_checkin;      /* True if >24h since last log */
} mood_status_t;

/** Initialize mood tracker (loads from NVS). */
int mood_tracker_init(void);

/** Log a mood value (1-10). Stores with current timestamp. */
void mood_tracker_log(uint8_t mood_value);

/** Get current mood status. Thread-safe. */
mood_status_t mood_tracker_get_status(void);

/** Check if a check-in reminder is due (>24h since last log). */
bool mood_tracker_needs_checkin(void);
