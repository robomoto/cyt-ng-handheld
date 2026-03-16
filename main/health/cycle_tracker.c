/**
 * Cycle / fertility tracker — calendar-based menstrual cycle tracking.
 *
 * Stores up to CYCLE_MAX_ENTRIES cycle start dates as epoch timestamps
 * in an NVS blob.  Calculates current cycle day, predicted fertile
 * window, average cycle length, and next period date.
 */

#include "cycle_tracker.h"

#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "cycle";

#define NVS_NAMESPACE   "cycle"
#define NVS_KEY_DATES   "dates"
#define NVS_KEY_BBT     "bbt"

#define DEFAULT_CYCLE_LEN   28
#define SECS_PER_DAY        86400

/* ── State ───────────────────────────────────────────────────────── */

static uint32_t          s_dates[CYCLE_MAX_ENTRIES];
static uint8_t           s_count;
static float             s_bbt_today;
static SemaphoreHandle_t s_mutex;
static bool              s_initialised = false;

/* ── Internal helpers ────────────────────────────────────────────── */

static uint32_t now_epoch(void)
{
    return (uint32_t)time(NULL);
}

static void save_dates(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        size_t blob_len = (size_t)s_count * sizeof(uint32_t);
        nvs_set_blob(h, NVS_KEY_DATES, s_dates, blob_len);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void load_dates(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t blob_len = sizeof(s_dates);
        if (nvs_get_blob(h, NVS_KEY_DATES, s_dates, &blob_len) == ESP_OK) {
            s_count = (uint8_t)(blob_len / sizeof(uint32_t));
        }
        nvs_close(h);
    }
}

/** Compute the rolling average cycle length from the stored intervals. */
static uint8_t calc_avg_cycle(void)
{
    if (s_count < 2) return DEFAULT_CYCLE_LEN;

    /* Use up to the last 6 cycle intervals */
    int start_idx = (s_count > 7) ? (s_count - 7) : 0;
    uint32_t total_days = 0;
    int intervals = 0;

    for (int i = start_idx; i < s_count - 1; i++) {
        uint32_t diff = s_dates[i + 1] - s_dates[i];
        uint32_t days = diff / SECS_PER_DAY;
        if (days >= 18 && days <= 45) {  /* Sanity check */
            total_days += days;
            intervals++;
        }
    }

    if (intervals == 0) return DEFAULT_CYCLE_LEN;
    return (uint8_t)(total_days / (uint32_t)intervals);
}

static void add_date(uint32_t epoch)
{
    /* If full, shift out the oldest */
    if (s_count >= CYCLE_MAX_ENTRIES) {
        memmove(&s_dates[0], &s_dates[1],
                (CYCLE_MAX_ENTRIES - 1) * sizeof(uint32_t));
        s_count = CYCLE_MAX_ENTRIES - 1;
    }
    s_dates[s_count++] = epoch;
    save_dates();
}

/* ── Public API ──────────────────────────────────────────────────── */

int cycle_tracker_init(void)
{
    if (s_initialised) return 0;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return -1;

    memset(s_dates, 0, sizeof(s_dates));
    s_count     = 0;
    s_bbt_today = 0.0f;

    load_dates();

    s_initialised = true;
    ESP_LOGI(TAG, "Cycle tracker loaded (%u entries)", s_count);
    return 0;
}

void cycle_tracker_log_period_start(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    add_date(now_epoch());
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Period start logged (total entries: %u)", s_count);
}

void cycle_tracker_log_period_start_on(uint32_t epoch_date)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    add_date(epoch_date);
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Period start logged on epoch %lu", (unsigned long)epoch_date);
}

void cycle_tracker_log_bbt(float temp_f)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_bbt_today = temp_f;
    xSemaphoreGive(s_mutex);
}

cycle_status_t cycle_tracker_get_status(void)
{
    cycle_status_t st;
    memset(&st, 0, sizeof(st));

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    st.cycle_count = s_count;
    st.bbt_today   = s_bbt_today;

    if (s_count == 0) {
        st.avg_cycle_length = DEFAULT_CYCLE_LEN;
        xSemaphoreGive(s_mutex);
        return st;
    }

    uint32_t last = s_dates[s_count - 1];
    st.last_period_date = last;

    uint8_t avg = calc_avg_cycle();
    st.avg_cycle_length = avg;

    /* Current cycle day */
    uint32_t now = now_epoch();
    uint32_t elapsed_days = (now > last) ? ((now - last) / SECS_PER_DAY) : 0;
    st.current_day = (uint8_t)((elapsed_days < 255) ? elapsed_days + 1 : 255);

    /* Fertile window adjusted for cycle length.
     * Standard: days 10-17 of a 28-day cycle.
     * Adjustment: shift by (avg - 28). */
    int adj = (int)avg - 28;
    int fw_start = 10 + adj;
    int fw_end   = 17 + adj;
    if (fw_start < 1) fw_start = 1;
    if (fw_end < fw_start) fw_end = fw_start + 7;

    st.fertile_window_start = (uint8_t)fw_start;
    st.fertile_window_end   = (uint8_t)fw_end;
    st.in_fertile_window    = (st.current_day >= fw_start &&
                               st.current_day <= fw_end);

    /* Next predicted period */
    st.next_period_date = last + (uint32_t)avg * SECS_PER_DAY;

    xSemaphoreGive(s_mutex);
    return st;
}

bool cycle_tracker_has_data(void)
{
    return s_count >= 2;
}
