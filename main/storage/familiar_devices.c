/**
 * Familiar device store — NVS-backed list of known/trusted device IDs.
 *
 * Storage strategy: the entire familiar list is kept as a single NVS blob
 * keyed "devices" in the "familiar" namespace.  With 256 max entries at
 * ~64 bytes each the blob tops out at ~16 KB — well within NVS limits.
 */

#include "familiar_devices.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "familiar";

/* ── Static state ────────────────────────────────────────────────── */

static familiar_entry_t s_entries[FAMILIAR_MAX_DEVICES];
static uint32_t         s_count = 0;
static bool             s_baseline_active = false;
static nvs_handle_t     s_nvs_handle = 0;
static bool             s_initialised = false;

/* ── Helpers ─────────────────────────────────────────────────────── */

/** Find an entry by device_id.  Returns index or -1. */
static int find_index(const uint8_t device_id[6])
{
    for (uint32_t i = 0; i < s_count; i++) {
        if (memcmp(s_entries[i].device_id, device_id, 6) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* ── Public API ──────────────────────────────────────────────────── */

int familiar_init(void)
{
    esp_err_t err = nvs_open("familiar", NVS_READWRITE, &s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return -1;
    }

    /* Try to load existing blob. */
    size_t blob_size = sizeof(s_entries);
    err = nvs_get_blob(s_nvs_handle, "devices", s_entries, &blob_size);
    if (err == ESP_OK) {
        s_count = (uint32_t)(blob_size / sizeof(familiar_entry_t));
        ESP_LOGI(TAG, "Loaded %lu familiar devices from NVS",
                 (unsigned long)s_count);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_count = 0;
        ESP_LOGI(TAG, "No familiar devices in NVS — starting fresh");
    } else {
        ESP_LOGW(TAG, "nvs_get_blob error: %s — starting fresh",
                 esp_err_to_name(err));
        s_count = 0;
    }

    s_initialised = true;
    return 0;
}

bool familiar_is_known(const uint8_t device_id[6])
{
    return find_index(device_id) >= 0;
}

void familiar_add(const uint8_t device_id[6], source_type_t source,
                  const char *device_hint, bool auto_baselined)
{
    int idx = find_index(device_id);
    if (idx >= 0) {
        /* Already exists — update hint if provided. */
        if (device_hint && device_hint[0]) {
            strncpy(s_entries[idx].device_hint, device_hint,
                    FAMILIAR_HINT_LEN - 1);
            s_entries[idx].device_hint[FAMILIAR_HINT_LEN - 1] = '\0';
        }
        familiar_save();
        return;
    }

    if (s_count >= FAMILIAR_MAX_DEVICES) {
        ESP_LOGW(TAG, "Familiar list full (%d max)", FAMILIAR_MAX_DEVICES);
        return;
    }

    familiar_entry_t *e = &s_entries[s_count];
    memset(e, 0, sizeof(*e));
    memcpy(e->device_id, device_id, 6);
    e->source_type = (uint8_t)source;
    e->auto_baselined = auto_baselined ? 1 : 0;

    if (device_hint && device_hint[0]) {
        strncpy(e->device_hint, device_hint, FAMILIAR_HINT_LEN - 1);
        e->device_hint[FAMILIAR_HINT_LEN - 1] = '\0';
    }

    s_count++;
    familiar_save();

    if (s_baseline_active && auto_baselined) {
        ESP_LOGI(TAG, "Baseline auto-learned device %02x%02x%02x%02x%02x%02x (%s)",
                 device_id[0], device_id[1], device_id[2],
                 device_id[3], device_id[4], device_id[5],
                 device_hint ? device_hint : "");
    }
}

void familiar_remove(const uint8_t device_id[6])
{
    int idx = find_index(device_id);
    if (idx < 0) {
        return;
    }

    /* Shift remaining entries down. */
    uint32_t uidx = (uint32_t)idx;
    if (uidx < s_count - 1) {
        memmove(&s_entries[uidx], &s_entries[uidx + 1],
                (s_count - uidx - 1) * sizeof(familiar_entry_t));
    }
    s_count--;
    memset(&s_entries[s_count], 0, sizeof(familiar_entry_t));

    familiar_save();
    ESP_LOGI(TAG, "Removed device %02x%02x%02x%02x%02x%02x — %lu remain",
             device_id[0], device_id[1], device_id[2],
             device_id[3], device_id[4], device_id[5],
             (unsigned long)s_count);
}

void familiar_set_label(const uint8_t device_id[6], const char *label)
{
    int idx = find_index(device_id);
    if (idx < 0) {
        ESP_LOGW(TAG, "set_label: device not in familiar list");
        return;
    }

    if (label) {
        strncpy(s_entries[idx].user_label, label, FAMILIAR_LABEL_LEN - 1);
        s_entries[idx].user_label[FAMILIAR_LABEL_LEN - 1] = '\0';
    } else {
        s_entries[idx].user_label[0] = '\0';
    }

    familiar_save();
}

const familiar_entry_t *familiar_get(const uint8_t device_id[6])
{
    int idx = find_index(device_id);
    if (idx < 0) {
        return NULL;
    }
    return &s_entries[idx];
}

uint32_t familiar_count(void)
{
    return s_count;
}

uint32_t familiar_baseline_count(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < s_count; i++) {
        if (s_entries[i].auto_baselined) {
            n++;
        }
    }
    return n;
}

void familiar_for_each(familiar_callback_t cb, void *ctx)
{
    if (!cb) {
        return;
    }
    for (uint32_t i = 0; i < s_count; i++) {
        cb(&s_entries[i], ctx);
    }
}

void familiar_save(void)
{
    if (!s_initialised) {
        return;
    }

    esp_err_t err = nvs_set_blob(s_nvs_handle, "devices",
                                  s_entries,
                                  s_count * sizeof(familiar_entry_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob failed: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_commit(s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    }
}

/* ── Baseline learning mode ──────────────────────────────────────── */

void familiar_start_baseline(void)
{
    s_baseline_active = true;
    ESP_LOGI(TAG, "Baseline learning started — auto-marking all detected devices");
}

uint32_t familiar_stop_baseline(void)
{
    s_baseline_active = false;
    uint32_t bc = familiar_baseline_count();
    ESP_LOGI(TAG, "Baseline learning stopped — %lu devices auto-learned",
             (unsigned long)bc);
    return bc;
}

bool familiar_is_baseline_active(void)
{
    return s_baseline_active;
}
