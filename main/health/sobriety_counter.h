/**
 * Sobriety counter — days/hours since a user-set start date.
 *
 * Simple but meaningful. User sets a sobriety date via phone companion
 * or long-press on the counter screen. Device counts days, hours, and
 * shows milestones (30 days, 60 days, 90 days, 6 months, 1 year, etc.).
 *
 * Data stored in NVS. Minimal — just the start date.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/** Sobriety counter status. */
typedef struct {
    uint32_t start_date;        /* Epoch timestamp of sobriety start */
    uint32_t days;              /* Days since start */
    uint32_t hours;             /* Total hours */
    bool     is_set;            /* True if a start date has been configured */
    const char *milestone;      /* Current milestone text, or NULL */
    uint32_t next_milestone_days; /* Days until next milestone */
} sobriety_status_t;

/** Initialize sobriety counter (loads from NVS). */
int sobriety_init(void);

/** Set the sobriety start date. Epoch timestamp. */
void sobriety_set_date(uint32_t epoch_date);

/** Reset (clear the start date). */
void sobriety_reset(void);

/** Get current status. Thread-safe. */
sobriety_status_t sobriety_get_status(void);
