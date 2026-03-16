/**
 * Pedometer — real step counting from QMI8658 accelerometer.
 *
 * The T-Display-S3 AMOLED includes a QMI8658 6-axis IMU on the I2C bus.
 * The pedometer reads the accelerometer at ~50Hz, applies a low-pass
 * filter, detects step peaks, and accumulates daily counts.
 *
 * Data persisted to NVS: daily step count, total distance, calories.
 * Resets at midnight (GPS-derived or manual time).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/** Pedometer statistics. */
typedef struct {
    uint32_t steps_today;
    uint32_t steps_total;       /* Lifetime total */
    float    distance_km;       /* Today, based on stride length */
    float    calories;          /* Today, rough estimate */
    float    stride_length_m;   /* Configurable, default 0.75m */
    bool     active;            /* Currently detecting movement */
} pedometer_stats_t;

/** Initialize QMI8658 accelerometer and start step detection task. */
int pedometer_init(void);

/** Get current pedometer statistics. Thread-safe. */
pedometer_stats_t pedometer_get_stats(void);

/** Reset daily counters (called at midnight or manually). */
void pedometer_reset_daily(void);

/** Set stride length in meters (default 0.75m). */
void pedometer_set_stride(float meters);

/** Set body weight in kg for calorie estimation (default 70kg). */
void pedometer_set_weight(float kg);
