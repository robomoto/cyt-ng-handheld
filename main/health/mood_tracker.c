/**
 * Mood tracker — daily mood logging (1-10) with 7-day trend analysis.
 *
 * Stores (timestamp, mood_value) pairs in an NVS blob, up to
 * MOOD_MAX_ENTRIES.  Calculates rolling 7-day average and trend
 * direction by comparing the last 3 days to the previous 3 days.
 */

#include "mood_tracker.h"

#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "mood";

#define NVS_NAMESPACE   "mood"
#define NVS_KEY_ENTRIES  "entries"

#define SECS_PER_HOUR   3600
#define SECS_PER_DAY    86400

/* ── Entry storage ───────────────────────────────────────────────── */

typedef struct {
    uint32_t timestamp;
    uint8_t  value;     /* 1-10 */
} mood_entry_t;

static mood_entry_t      s_entries[MOOD_MAX_ENTRIES];
static uint16_t          s_count;
static SemaphoreHandle_t s_mutex;
static bool              s_initialised = false;

/* ── NVS persistence ─────────────────────────────────────────────── */

static void save_entries(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        size_t blob_len = (size_t)s_count * sizeof(mood_entry_t);
        nvs_set_blob(h, NVS_KEY_ENTRIES, s_entries, blob_len);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void load_entries(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t blob_len = sizeof(s_entries);
        if (nvs_get_blob(h, NVS_KEY_ENTRIES, s_entries, &blob_len) == ESP_OK) {
            s_count = (uint16_t)(blob_len / sizeof(mood_entry_t));
        }
        nvs_close(h);
    }
}

static uint32_t now_epoch(void)
{
    return (uint32_t)time(NULL);
}

/* ── Trend calculation ───────────────────────────────────────────── */

/**
 * Compute the average mood of entries within [start_epoch, end_epoch).
 * Returns 0 if no entries match.
 */
static float avg_in_range(uint32_t start_epoch, uint32_t end_epoch,
                          int *out_count)
{
    float sum = 0;
    int n = 0;
    for (int i = 0; i < s_count; i++) {
        if (s_entries[i].timestamp >= start_epoch &&
            s_entries[i].timestamp <  end_epoch) {
            sum += (float)s_entries[i].value;
            n++;
        }
    }
    if (out_count) *out_count = n;
    return (n > 0) ? (sum / (float)n) : 0.0f;
}

/* ── Public API ──────────────────────────────────────────────────── */

int mood_tracker_init(void)
{
    if (s_initialised) return 0;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return -1;

    memset(s_entries, 0, sizeof(s_entries));
    s_count = 0;

    load_entries();

    s_initialised = true;
    ESP_LOGI(TAG, "Mood tracker loaded (%u entries)", s_count);
    return 0;
}

void mood_tracker_log(uint8_t mood_value)
{
    if (mood_value < 1) mood_value = 1;
    if (mood_value > 10) mood_value = 10;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* If at capacity, drop the oldest */
    if (s_count >= MOOD_MAX_ENTRIES) {
        memmove(&s_entries[0], &s_entries[1],
                (MOOD_MAX_ENTRIES - 1) * sizeof(mood_entry_t));
        s_count = MOOD_MAX_ENTRIES - 1;
    }

    s_entries[s_count].timestamp = now_epoch();
    s_entries[s_count].value     = mood_value;
    s_count++;

    save_entries();

    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Mood logged: %u (total: %u)", mood_value, s_count);
}

mood_status_t mood_tracker_get_status(void)
{
    mood_status_t st;
    memset(&st, 0, sizeof(st));

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    st.total_entries = s_count;

    if (s_count == 0) {
        st.trend        = MOOD_TREND_UNKNOWN;
        st.needs_checkin = true;
        xSemaphoreGive(s_mutex);
        return st;
    }

    /* Last entry */
    st.last_mood     = s_entries[s_count - 1].value;
    st.last_log_time = s_entries[s_count - 1].timestamp;

    uint32_t now = now_epoch();

    /* Hours since last log */
    uint32_t elapsed_s = (now > st.last_log_time) ?
                         (now - st.last_log_time) : 0;
    st.hours_since_last = (uint16_t)(elapsed_s / SECS_PER_HOUR);
    st.needs_checkin    = (elapsed_s > (uint32_t)SECS_PER_DAY);

    /* 7-day rolling average */
    uint32_t week_ago = now - 7 * SECS_PER_DAY;
    int week_n = 0;
    st.avg_7day = avg_in_range(week_ago, now + 1, &week_n);

    /* Trend: compare last 3 days avg to previous 3 days avg */
    uint32_t three_days   = 3 * SECS_PER_DAY;
    uint32_t recent_start = now - three_days;
    uint32_t prev_start   = now - 2 * three_days;

    int n_recent = 0, n_prev = 0;
    float avg_recent = avg_in_range(recent_start, now + 1, &n_recent);
    float avg_prev   = avg_in_range(prev_start, recent_start, &n_prev);

    if (n_recent == 0 || n_prev == 0) {
        st.trend = MOOD_TREND_UNKNOWN;
    } else {
        float diff = avg_recent - avg_prev;
        if (diff > 0.5f) {
            st.trend = MOOD_TREND_IMPROVING;
        } else if (diff < -0.5f) {
            st.trend = MOOD_TREND_DECLINING;
        } else {
            st.trend = MOOD_TREND_STABLE;
        }
    }

    xSemaphoreGive(s_mutex);
    return st;
}

bool mood_tracker_needs_checkin(void)
{
    mood_status_t st = mood_tracker_get_status();
    return st.needs_checkin;
}
