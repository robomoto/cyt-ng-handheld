/**
 * Sobriety counter — elapsed time since a user-set start date.
 *
 * Stores a single epoch timestamp in NVS.  Counts days and hours,
 * reports the most recently passed milestone and days until the next.
 */

#include "sobriety_counter.h"

#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "sobriety";

#define NVS_NAMESPACE   "sobriety"
#define NVS_KEY_START   "start"

#define SECS_PER_DAY    86400

/* ── Milestone table ─────────────────────────────────────────────── */

typedef struct {
    uint32_t    days;
    const char *label;
} milestone_entry_t;

static const milestone_entry_t MILESTONES[] = {
    {    1, "1 Day"       },
    {    7, "1 Week"      },
    {   14, "2 Weeks"     },
    {   30, "1 Month"     },
    {   60, "2 Months"    },
    {   90, "90 Days"     },
    {  120, "4 Months"    },
    {  180, "6 Months"    },
    {  270, "9 Months"    },
    {  365, "1 Year"      },
    {  548, "18 Months"   },
    {  730, "2 Years"     },
    { 1095, "3 Years"     },
    { 1825, "5 Years"     },
    { 3650, "10 Years"    },
};

#define MILESTONE_COUNT  (sizeof(MILESTONES) / sizeof(MILESTONES[0]))

/* ── State ───────────────────────────────────────────────────────── */

static uint32_t          s_start_date;
static bool              s_is_set;
static SemaphoreHandle_t s_mutex;
static bool              s_initialised = false;

/* ── NVS persistence ─────────────────────────────────────────────── */

static void save_date(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        if (s_is_set) {
            nvs_set_u32(h, NVS_KEY_START, s_start_date);
        } else {
            nvs_erase_key(h, NVS_KEY_START);
        }
        nvs_commit(h);
        nvs_close(h);
    }
}

static void load_date(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u32(h, NVS_KEY_START, &s_start_date) == ESP_OK) {
            s_is_set = true;
        }
        nvs_close(h);
    }
}

static uint32_t now_epoch(void)
{
    return (uint32_t)time(NULL);
}

/* ── Public API ──────────────────────────────────────────────────── */

int sobriety_init(void)
{
    if (s_initialised) return 0;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return -1;

    s_start_date = 0;
    s_is_set     = false;

    load_date();

    s_initialised = true;
    if (s_is_set) {
        ESP_LOGI(TAG, "Sobriety counter loaded (start: %lu)",
                 (unsigned long)s_start_date);
    } else {
        ESP_LOGI(TAG, "Sobriety counter: no date set");
    }
    return 0;
}

void sobriety_set_date(uint32_t epoch_date)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_start_date = epoch_date;
    s_is_set     = true;
    save_date();
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Start date set to %lu", (unsigned long)epoch_date);
}

void sobriety_reset(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_start_date = 0;
    s_is_set     = false;
    save_date();
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Counter reset");
}

sobriety_status_t sobriety_get_status(void)
{
    sobriety_status_t st;
    memset(&st, 0, sizeof(st));

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    st.is_set     = s_is_set;
    st.start_date = s_start_date;

    if (!s_is_set) {
        xSemaphoreGive(s_mutex);
        return st;
    }

    uint32_t now = now_epoch();
    uint32_t elapsed = (now > s_start_date) ? (now - s_start_date) : 0;

    st.days  = elapsed / SECS_PER_DAY;
    st.hours = elapsed / 3600;

    /* Find the most recently passed milestone and the next one */
    st.milestone           = NULL;
    st.next_milestone_days = 0;

    for (int i = (int)MILESTONE_COUNT - 1; i >= 0; i--) {
        if (st.days >= MILESTONES[i].days) {
            st.milestone = MILESTONES[i].label;

            /* Next milestone */
            if (i + 1 < (int)MILESTONE_COUNT) {
                st.next_milestone_days = MILESTONES[i + 1].days - st.days;
            }
            break;
        }
    }

    /* If no milestone passed yet, next is "1 Day" */
    if (!st.milestone && st.days < MILESTONES[0].days) {
        st.next_milestone_days = MILESTONES[0].days - st.days;
    }

    xSemaphoreGive(s_mutex);
    return st;
}
