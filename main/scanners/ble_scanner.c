/**
 * BLE scanner — detects AirTags, SmartTags, Tile, and Remote ID drones.
 *
 * Uses NimBLE GAP passive scanning. Advertisements are classified by
 * manufacturer-specific data (type 0xFF) and service data (type 0x16).
 * Detected trackers are pushed to a FreeRTOS queue for the analysis task.
 */

#include "ble_scanner.h"
#include "remote_id.h"

#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "mbedtls/sha256.h"

static const char *TAG = "ble_scan";

/* ── Queue ───────────────────────────────────────────────────────── */

#define BLE_QUEUE_SIZE  64

static QueueHandle_t s_ble_queue = NULL;
static bool          s_scanning  = false;

/* ── Company IDs (BLE SIG assigned numbers, little-endian in adv) ─ */

#define COMPANY_APPLE    0x004C
#define COMPANY_SAMSUNG  0x0075
#define COMPANY_GOOGLE   0x00E0

/* ── Service UUIDs ───────────────────────────────────────────────── */

#define SERVICE_UUID_TILE       0xFFFE
#define SERVICE_UUID_REMOTE_ID  0xFFFA

/* ── Helpers ─────────────────────────────────────────────────────── */

/**
 * Compute a 6-byte device_id by SHA-256 hashing the payload data and
 * taking the first 6 bytes.
 */
static void compute_device_id(const uint8_t *data, uint16_t len,
                               uint8_t device_id[6])
{
    uint8_t hash[32];
    mbedtls_sha256(data, len, hash, 0 /* SHA-256, not SHA-224 */);
    memcpy(device_id, hash, 6);
}

/**
 * Fill common fields of a ble_detection_t and push to the queue.
 */
static void push_detection(const uint8_t *mac, int8_t rssi,
                           const char *tracker_type,
                           const uint8_t *payload, uint16_t payload_len)
{
    if (!s_ble_queue) {
        return;
    }

    ble_detection_t det;
    memset(&det, 0, sizeof(det));

    compute_device_id(payload, payload_len, det.device_id);
    strncpy(det.tracker_type, tracker_type, sizeof(det.tracker_type) - 1);
    det.rssi = rssi;
    memcpy(det.mac, mac, 6);
    det.is_remote_id = false;

    xQueueSend(s_ble_queue, &det, 0);
}

/**
 * Push a Remote ID detection with drone-specific fields.
 */
static void push_remote_id(const uint8_t *mac, int8_t rssi,
                           const remote_id_data_t *rid)
{
    if (!s_ble_queue) {
        return;
    }

    ble_detection_t det;
    memset(&det, 0, sizeof(det));

    /* Use serial as basis for device_id */
    compute_device_id((const uint8_t *)rid->serial, strlen(rid->serial),
                      det.device_id);
    strncpy(det.tracker_type, "drone", sizeof(det.tracker_type) - 1);
    det.rssi = rssi;
    memcpy(det.mac, mac, 6);
    det.is_remote_id = true;
    memcpy(det.drone_serial, rid->serial, sizeof(det.drone_serial));
    det.drone_lat    = rid->drone_lat;
    det.drone_lon    = rid->drone_lon;
    det.drone_alt    = rid->drone_alt;
    det.operator_lat = rid->operator_lat;
    det.operator_lon = rid->operator_lon;

    xQueueSend(s_ble_queue, &det, 0);
}

/* ── Advertisement field parsing ─────────────────────────────────── */

/**
 * Walk raw advertisement data looking for specific AD types.
 * BLE advertisement fields: [length][type][data...]
 *
 * Returns pointer to field data (after type byte) and sets *out_len
 * to the data length. Returns NULL if not found.
 */
static const uint8_t *find_ad_field(const uint8_t *adv_data, uint8_t adv_len,
                                     uint8_t ad_type, uint8_t *out_len)
{
    uint8_t offset = 0;
    while (offset < adv_len) {
        uint8_t field_len = adv_data[offset];
        if (field_len == 0 || offset + field_len >= adv_len) {
            break;
        }
        uint8_t field_type = adv_data[offset + 1];
        if (field_type == ad_type) {
            *out_len = field_len - 1; /* exclude type byte */
            return &adv_data[offset + 2];
        }
        offset += field_len + 1;
    }
    *out_len = 0;
    return NULL;
}

/**
 * Walk raw advertisement data and call a callback for each occurrence
 * of the given AD type. Used when multiple fields of the same type
 * may be present (e.g., multiple service data entries).
 */
typedef void (*ad_field_cb_t)(const uint8_t *data, uint8_t len, void *ctx);

static void foreach_ad_field(const uint8_t *adv_data, uint8_t adv_len,
                             uint8_t ad_type, ad_field_cb_t cb, void *ctx)
{
    uint8_t offset = 0;
    while (offset < adv_len) {
        uint8_t field_len = adv_data[offset];
        if (field_len == 0 || offset + field_len >= adv_len) {
            break;
        }
        uint8_t field_type = adv_data[offset + 1];
        if (field_type == ad_type) {
            cb(&adv_data[offset + 2], field_len - 1, ctx);
        }
        offset += field_len + 1;
    }
}

/* ── Manufacturer data classification ────────────────────────────── */

/**
 * Classify manufacturer-specific data (AD type 0xFF).
 * First 2 bytes are company ID (little-endian), rest is payload.
 */
static bool classify_manufacturer(const uint8_t *data, uint8_t len,
                                   const uint8_t *mac, int8_t rssi)
{
    if (len < 3) {
        return false; /* Need at least company ID + 1 byte */
    }

    uint16_t company_id = data[0] | ((uint16_t)data[1] << 8);
    const uint8_t *payload = &data[2];
    uint8_t payload_len = len - 2;

    switch (company_id) {
    case COMPANY_APPLE:
        if (payload_len >= 1) {
            uint8_t type_byte = payload[0];
            if (type_byte == 0x12 && payload_len >= 25) {
                /* AirTag / Find My network */
                push_detection(mac, rssi, "findmy", data, len);
                return true;
            }
            if (type_byte == 0x07) {
                /* AirPods / Find My Nearby */
                push_detection(mac, rssi, "findmy_nearby", data, len);
                return true;
            }
        }
        break;

    case COMPANY_SAMSUNG:
        if (payload_len >= 4) {
            push_detection(mac, rssi, "smarttag", data, len);
            return true;
        }
        break;

    case COMPANY_GOOGLE:
        push_detection(mac, rssi, "google_findmy", data, len);
        return true;
    }

    return false;
}

/* ── Service data classification ─────────────────────────────────── */

typedef struct {
    const uint8_t *mac;
    int8_t         rssi;
    bool           handled;
} svc_ctx_t;

/**
 * Callback for each service data field (AD type 0x16).
 * First 2 bytes are the 16-bit UUID (little-endian).
 */
static void classify_service_data(const uint8_t *data, uint8_t len,
                                   void *ctx)
{
    svc_ctx_t *sctx = (svc_ctx_t *)ctx;
    if (len < 3) {
        return; /* UUID + at least 1 data byte */
    }

    uint16_t uuid = data[0] | ((uint16_t)data[1] << 8);
    const uint8_t *svc_payload = &data[2];
    uint8_t svc_len = len - 2;

    switch (uuid) {
    case SERVICE_UUID_TILE:
        push_detection(sctx->mac, sctx->rssi, "tile", data, len);
        sctx->handled = true;
        break;

    case SERVICE_UUID_REMOTE_ID: {
        remote_id_data_t rid;
        if (remote_id_parse_ble(svc_payload, svc_len, &rid)) {
            push_remote_id(sctx->mac, sctx->rssi, &rid);
            sctx->handled = true;
        }
        break;
    }
    }
}

/* ── GAP event callback ──────────────────────────────────────────── */

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    if (event->type != BLE_GAP_EVENT_DISC) {
        return 0;
    }

    const struct ble_gap_disc_desc *desc = &event->disc;
    const uint8_t *adv_data = desc->data;
    uint8_t adv_len = desc->length_data;
    int8_t rssi = desc->rssi;
    const uint8_t *mac = desc->addr.val;

    /* Try manufacturer-specific data (AD type 0xFF) */
    uint8_t mfr_len = 0;
    const uint8_t *mfr_data = find_ad_field(adv_data, adv_len, 0xFF,
                                             &mfr_len);
    if (mfr_data && mfr_len > 0) {
        if (classify_manufacturer(mfr_data, mfr_len, mac, rssi)) {
            return 0; /* Handled */
        }
    }

    /* Try service data (AD type 0x16) — may have multiple entries */
    svc_ctx_t sctx = {
        .mac     = mac,
        .rssi    = rssi,
        .handled = false,
    };
    foreach_ad_field(adv_data, adv_len, 0x16, classify_service_data, &sctx);

    return 0;
}

/* ── Public API ──────────────────────────────────────────────────── */

void ble_scanner_start(void)
{
    /* Create queue on first call */
    if (!s_ble_queue) {
        s_ble_queue = xQueueCreate(BLE_QUEUE_SIZE, sizeof(ble_detection_t));
        if (!s_ble_queue) {
            ESP_LOGE(TAG, "Failed to create BLE detection queue");
            return;
        }
    }

    if (s_scanning) {
        return;
    }

    /*
     * Ensure NimBLE host is synced. On ESP-IDF with NimBLE, the host
     * is initialized by nimble_port_init() / nimble_port_freertos_init()
     * which should be called once at startup (typically by the BLE
     * controller init path). We check ble_hs_synced() to confirm.
     */
    if (!ble_hs_synced()) {
        ESP_LOGW(TAG, "NimBLE host not synced yet, deferring scan start");
        return;
    }

    /* Passive scan: interval=100ms, window=100ms (continuous) */
    struct ble_gap_disc_params scan_params = {
        .itvl           = BLE_GAP_SCAN_ITVL_MS(100),
        .window         = BLE_GAP_SCAN_WIN_MS(100),
        .filter_policy  = BLE_HCI_SCAN_FILT_NO_WL,
        .limited        = 0,
        .passive        = 1,
        .filter_duplicates = 0,
    };

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER,
                          &scan_params, ble_gap_event_cb, NULL);
    if (rc == 0) {
        s_scanning = true;
        ESP_LOGI(TAG, "BLE scanning started (passive, continuous)");
    } else {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
    }
}

void ble_scanner_stop(void)
{
    if (!s_scanning) {
        return;
    }

    int rc = ble_gap_disc_cancel();
    if (rc == 0 || rc == BLE_HS_EALREADY) {
        s_scanning = false;
        ESP_LOGI(TAG, "BLE scanning stopped");
    } else {
        ESP_LOGE(TAG, "ble_gap_disc_cancel failed: %d", rc);
    }
}

void *ble_scanner_get_queue(void)
{
    /* Ensure queue exists even if called before start */
    if (!s_ble_queue) {
        s_ble_queue = xQueueCreate(BLE_QUEUE_SIZE, sizeof(ble_detection_t));
    }
    return (void *)s_ble_queue;
}
