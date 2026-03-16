/**
 * Device tracking table — PSRAM record array with SRAM hash index.
 *
 * Hash table uses separate chaining via a parallel next-pointer array.
 * Records are 48-byte packed structs stored contiguously in PSRAM.
 */

#include "device_table.h"
#include "../cyt_config.h"

#include <string.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

static const char *TAG = "dev_table";

/* ── Module state ──────────────────────────────────────────────────── */

/** Device record array in PSRAM — up to CYT_MAX_DEVICES entries. */
static device_record_t *s_records = NULL;

/** Hash bucket heads in SRAM — each stores index into s_records or -1. */
static int32_t *s_buckets = NULL;

/** Chain pointers in PSRAM — s_next[i] is index of next record in the
 *  same bucket, or -1 if end of chain. */
static int32_t *s_next = NULL;

/** Number of records currently allocated. */
static uint32_t s_record_count = 0;

/* ── Hash function ─────────────────────────────────────────────────── */

/**
 * Hash a 6-byte device_id into a bucket index [0, CYT_HASH_BUCKETS).
 *
 * XOR bytes 0-2 with bytes 3-5 to produce a 3-byte value,
 * then fold to 13 bits (8192 buckets).
 */
static uint32_t hash_id(const uint8_t id[6])
{
    uint8_t h0 = id[0] ^ id[3];
    uint8_t h1 = id[1] ^ id[4];
    uint8_t h2 = id[2] ^ id[5];

    uint32_t combined = ((uint32_t)h0 << 16) | ((uint32_t)h1 << 8) | h2;
    return combined & (CYT_HASH_BUCKETS - 1);  /* 8192 = 2^13, mask = 0x1FFF */
}

/* ── Bit-count helper ──────────────────────────────────────────────── */

static inline int popcount8(uint8_t v)
{
    int c = 0;
    while (v) {
        c += v & 1;
        v >>= 1;
    }
    return c;
}

/* ── Public API ────────────────────────────────────────────────────── */

int device_table_init(void)
{
    /* Allocate device records in PSRAM */
    s_records = heap_caps_malloc(CYT_MAX_DEVICES * sizeof(device_record_t),
                                 MALLOC_CAP_SPIRAM);
    if (!s_records) {
        ESP_LOGE(TAG, "Failed to allocate device records in PSRAM (%u bytes)",
                 (unsigned)(CYT_MAX_DEVICES * sizeof(device_record_t)));
        return -1;
    }

    /* Allocate chain pointers in PSRAM (alongside records) */
    s_next = heap_caps_malloc(CYT_MAX_DEVICES * sizeof(int32_t),
                              MALLOC_CAP_SPIRAM);
    if (!s_next) {
        ESP_LOGE(TAG, "Failed to allocate chain array in PSRAM");
        heap_caps_free(s_records);
        s_records = NULL;
        return -1;
    }

    /* Allocate hash buckets in SRAM for fast lookups */
    s_buckets = malloc(CYT_HASH_BUCKETS * sizeof(int32_t));
    if (!s_buckets) {
        ESP_LOGE(TAG, "Failed to allocate hash buckets in SRAM (%u bytes)",
                 (unsigned)(CYT_HASH_BUCKETS * sizeof(int32_t)));
        heap_caps_free(s_next);
        heap_caps_free(s_records);
        s_records = NULL;
        s_next = NULL;
        return -1;
    }

    /* Initialize all buckets to empty */
    for (uint32_t i = 0; i < CYT_HASH_BUCKETS; i++) {
        s_buckets[i] = -1;
    }

    s_record_count = 0;

    ESP_LOGI(TAG, "Device table initialized: %u record slots (%u KB PSRAM), "
             "%u hash buckets (%u KB SRAM)",
             CYT_MAX_DEVICES,
             (unsigned)(CYT_MAX_DEVICES * sizeof(device_record_t) / 1024),
             CYT_HASH_BUCKETS,
             (unsigned)(CYT_HASH_BUCKETS * sizeof(int32_t) / 1024));

    return 0;
}

device_record_t *device_table_lookup(const uint8_t id[6])
{
    if (!s_records || !s_buckets) {
        return NULL;
    }

    uint32_t bucket = hash_id(id);
    int32_t idx = s_buckets[bucket];

    while (idx >= 0) {
        if (memcmp(s_records[idx].device_id, id, 6) == 0) {
            return &s_records[idx];
        }
        idx = s_next[idx];
    }

    return NULL;
}

device_record_t *device_table_upsert(const uint8_t id[6], source_type_t source)
{
    if (!s_records || !s_buckets) {
        return NULL;
    }

    /* Try to find existing record */
    device_record_t *existing = device_table_lookup(id);
    if (existing) {
        if (existing->appearance_count < 255) {
            existing->appearance_count++;
        }
        /* Mark current time window (bit 0) as active */
        existing->window_flags |= 0x01;
        return existing;
    }

    /* Table full? */
    if (s_record_count >= CYT_MAX_DEVICES) {
        ESP_LOGW(TAG, "Device table full (%u records)", s_record_count);
        return NULL;
    }

    /* Allocate new record in next free slot */
    uint32_t slot = s_record_count++;
    device_record_t *rec = &s_records[slot];

    memset(rec, 0, sizeof(*rec));
    memcpy(rec->device_id, id, 6);
    rec->source_type = (uint8_t)source;
    rec->appearance_count = 1;
    rec->window_flags = 0x01;  /* Present in current window */

    /* Use GPS timestamp if available, otherwise uptime */
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000);
    rec->first_seen = now;
    rec->last_seen = now;

    /* Link into hash bucket (prepend) */
    uint32_t bucket = hash_id(id);
    s_next[slot] = s_buckets[bucket];
    s_buckets[bucket] = (int32_t)slot;

    return rec;
}

void device_table_rotate_windows(void)
{
    for (uint32_t i = 0; i < s_record_count; i++) {
        s_records[i].window_flags >>= 1;
    }

    ESP_LOGD(TAG, "Window flags rotated for %u devices", (unsigned)s_record_count);
}

uint32_t device_table_active_count(void)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < s_record_count; i++) {
        if (s_records[i].appearance_count > 0) {
            count++;
        }
    }
    return count;
}

uint32_t device_table_suspicious_count(void)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < s_record_count; i++) {
        if (popcount8(s_records[i].window_flags) >= 2) {
            count++;
        }
    }
    return count;
}

void device_table_for_each_suspicious(uint8_t min_bits, device_callback_t cb, void *ctx)
{
    if (!cb || !s_records) {
        return;
    }

    for (uint32_t i = 0; i < s_record_count; i++) {
        if (popcount8(s_records[i].window_flags) >= min_bits) {
            cb(&s_records[i], ctx);
        }
    }
}
