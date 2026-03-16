/**
 * BLE GATT companion service — streams alerts and status to a phone/watch.
 *
 * Runs as a NimBLE peripheral (GATT server) coexisting with the BLE scanner's
 * GAP observer role. NimBLE supports both simultaneously; some scan packet
 * loss (~10-15%) is expected when a central is connected.
 *
 * GATT layout:
 *   Service  0000ff10-0000-1000-8000-00805f9b34fb
 *     Alert  ...ff11  notify          JSON alert on tracker detection
 *     Status ...ff12  read + notify   JSON status blob
 *     Devs   ...ff13  read            JSON array of top 10 suspicious devices
 *     Cmd    ...ff14  write           JSON commands from phone
 */

#include "ble_companion.h"
#include "cyt_config.h"
#include "storage/familiar_devices.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_comp";

/* ── UUIDs ────────────────────────────────────────────────────────── */

/* Service: 0000ff10-0000-1000-8000-00805f9b34fb */
static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x10, 0xff, 0x00, 0x00);

/* Alert Stream: ...ff11 */
static const ble_uuid128_t s_alert_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x11, 0xff, 0x00, 0x00);

/* Status: ...ff12 */
static const ble_uuid128_t s_status_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x12, 0xff, 0x00, 0x00);

/* Device List: ...ff13 */
static const ble_uuid128_t s_devlist_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x13, 0xff, 0x00, 0x00);

/* Command: ...ff14 */
static const ble_uuid128_t s_cmd_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0x14, 0xff, 0x00, 0x00);

/* ── State ────────────────────────────────────────────────────────── */

static uint16_t s_conn_handle   = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_alert_handle  = 0;  /* Populated by NimBLE after svc add */
static uint16_t s_status_handle = 0;

static bool s_alert_subscribed  = false;
static bool s_status_subscribed = false;

static companion_cmd_handler_t s_cmd_handler = NULL;

/* Cached status JSON — updated by ble_companion_update_status(). */
#define STATUS_BUF_SIZE  256
static char   s_status_buf[STATUS_BUF_SIZE];
static size_t s_status_len = 0;

/* Cached device-list JSON — updated when read. */
#define DEVLIST_BUF_SIZE  1024
static char   s_devlist_buf[DEVLIST_BUF_SIZE];
static size_t s_devlist_len = 0;

/* Protects s_status_buf from concurrent access. */
static SemaphoreHandle_t s_status_mutex = NULL;

/* ── Familiar-device command helpers ──────────────────────────────── */

/**
 * Parse a hex MAC string "AA:BB:CC:DD:EE:FF" or "aabbccddeeff" into 6 bytes.
 * Returns true on success.
 */
static bool parse_device_id(const char *str, uint8_t out[6])
{
    if (!str) return false;

    /* Try colon-separated first (AA:BB:CC:DD:EE:FF = 17 chars) */
    unsigned int b[6];
    if (strlen(str) >= 17 &&
        sscanf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
        for (int i = 0; i < 6; i++) out[i] = (uint8_t)b[i];
        return true;
    }
    /* Try plain hex (aabbccddeeff = 12 chars) */
    if (strlen(str) >= 12 &&
        sscanf(str, "%02x%02x%02x%02x%02x%02x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
        for (int i = 0; i < 6; i++) out[i] = (uint8_t)b[i];
        return true;
    }
    return false;
}

/**
 * Extract a JSON string value for the given key using strstr.
 * Writes the value (without quotes) into out, up to out_len-1 chars.
 * Returns true on success.
 */
static bool json_extract_str(const char *json, const char *key,
                              char *out, size_t out_len)
{
    /* Build "key":" search pattern */
    char pattern[48];
    int plen = snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    if (plen <= 0 || (size_t)plen >= sizeof(pattern)) return false;

    const char *p = strstr(json, pattern);
    if (!p) return false;

    p += plen; /* skip past opening quote */
    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

/** Context for building familiar device list JSON. */
typedef struct {
    char  *buf;
    size_t cap;
    size_t pos;
    int    count;
} familiar_list_ctx_t;

static void familiar_list_cb(const familiar_entry_t *entry, void *ctx)
{
    familiar_list_ctx_t *fl = (familiar_list_ctx_t *)ctx;

    if (fl->count > 0 && fl->pos < fl->cap - 1) {
        fl->buf[fl->pos++] = ',';
    }

    char id_hex[18];
    snprintf(id_hex, sizeof(id_hex), "%02x:%02x:%02x:%02x:%02x:%02x",
             entry->device_id[0], entry->device_id[1], entry->device_id[2],
             entry->device_id[3], entry->device_id[4], entry->device_id[5]);

    int n = snprintf(fl->buf + fl->pos, fl->cap - fl->pos,
                     "{\"id\":\"%s\",\"hint\":\"%s\",\"label\":\"%s\",\"auto\":%s}",
                     id_hex,
                     entry->device_hint,
                     entry->user_label,
                     entry->auto_baselined ? "true" : "false");
    if (n > 0 && (size_t)n < fl->cap - fl->pos) {
        fl->pos += (size_t)n;
    }
    fl->count++;
}

/**
 * Handle familiar-device commands received over the GATT command characteristic.
 * Called from inside gatt_access_cb after the external cmd_handler.
 */
static void handle_familiar_cmd(const char *cmd_buf, uint16_t len)
{
    /* ── mark_familiar ─────────────────────────────────────────── */
    if (strstr(cmd_buf, "\"mark_familiar\"")) {
        char id_str[24] = {0};
        char label[FAMILIAR_LABEL_LEN] = {0};
        uint8_t dev_id[6];

        if (!json_extract_str(cmd_buf, "id", id_str, sizeof(id_str)) ||
            !parse_device_id(id_str, dev_id)) {
            ESP_LOGW(TAG, "mark_familiar: bad/missing id");
            return;
        }

        /* Determine source_type from device table if possible */
        source_type_t src = SOURCE_BLE; /* default */
        const device_record_t *rec = device_table_lookup(dev_id);
        if (rec) {
            src = (source_type_t)rec->source_type;
        }

        familiar_add(dev_id, src, NULL, false);

        /* Set label if provided */
        if (json_extract_str(cmd_buf, "label", label, sizeof(label))) {
            familiar_set_label(dev_id, label);
        }

        ESP_LOGI(TAG, "mark_familiar: %s label=\"%s\"", id_str, label);
        return;
    }

    /* ── unmark_familiar ───────────────────────────────────────── */
    if (strstr(cmd_buf, "\"unmark_familiar\"")) {
        char id_str[24] = {0};
        uint8_t dev_id[6];

        if (!json_extract_str(cmd_buf, "id", id_str, sizeof(id_str)) ||
            !parse_device_id(id_str, dev_id)) {
            ESP_LOGW(TAG, "unmark_familiar: bad/missing id");
            return;
        }

        familiar_remove(dev_id);
        ESP_LOGI(TAG, "unmark_familiar: %s", id_str);
        return;
    }

    /* ── list_familiar ─────────────────────────────────────────── */
    if (strstr(cmd_buf, "\"list_familiar\"")) {
        /*
         * Build a JSON array of familiar devices and write it into
         * s_devlist_buf so the phone can read it via the Device List
         * characteristic.
         */
        s_devlist_buf[0] = '[';
        familiar_list_ctx_t fl = {
            .buf   = s_devlist_buf + 1,
            .cap   = DEVLIST_BUF_SIZE - 2,
            .pos   = 0,
            .count = 0,
        };

        familiar_for_each(familiar_list_cb, &fl);

        size_t total = 1 + fl.pos;
        if (total < DEVLIST_BUF_SIZE - 1) {
            s_devlist_buf[total] = ']';
            total++;
        }
        s_devlist_buf[total] = '\0';
        s_devlist_len = total;

        ESP_LOGI(TAG, "list_familiar: %d devices, %zu bytes",
                 fl.count, s_devlist_len);
        return;
    }
}

/* ── Device list builder ──────────────────────────────────────────── */

typedef struct {
    char  *buf;
    size_t cap;
    size_t pos;
    int    count;
} devlist_ctx_t;

#define DEVLIST_MAX 10

static void devlist_cb(const device_record_t *rec, void *ctx)
{
    devlist_ctx_t *dl = (devlist_ctx_t *)ctx;
    if (dl->count >= DEVLIST_MAX) {
        return;
    }

    /* Separator */
    if (dl->count > 0 && dl->pos < dl->cap - 1) {
        dl->buf[dl->pos++] = ',';
    }

    /* Format device_id as hex */
    char id_hex[13];
    snprintf(id_hex, sizeof(id_hex), "%02x%02x%02x%02x%02x%02x",
             rec->device_id[0], rec->device_id[1], rec->device_id[2],
             rec->device_id[3], rec->device_id[4], rec->device_id[5]);

    /* Count window bits for a rough persistence proxy */
    uint8_t wbits = 0;
    uint8_t wf = rec->window_flags;
    while (wf) { wbits += wf & 1; wf >>= 1; }

    int n = snprintf(dl->buf + dl->pos, dl->cap - dl->pos,
                     "{\"id\":\"%s\",\"rssi\":%d,\"wins\":%u,\"seen\":%lu}",
                     id_hex, (int)rec->rssi_avg, (unsigned)wbits,
                     (unsigned long)rec->last_seen);
    if (n > 0 && (size_t)n < dl->cap - dl->pos) {
        dl->pos += (size_t)n;
    }
    dl->count++;
}

/**
 * Rebuild s_devlist_buf by iterating suspicious devices.
 */
static void rebuild_devlist(void)
{
    devlist_ctx_t dl = {
        .buf   = s_devlist_buf + 1,  /* leave room for opening '[' */
        .cap   = DEVLIST_BUF_SIZE - 2, /* room for '[' and ']' */
        .pos   = 0,
        .count = 0,
    };

    s_devlist_buf[0] = '[';

    device_table_for_each_suspicious(CYT_MULTI_WINDOW_ALERT_BITS, devlist_cb, &dl);

    size_t total = 1 + dl.pos;
    if (total < DEVLIST_BUF_SIZE - 1) {
        s_devlist_buf[total] = ']';
        total++;
    }
    s_devlist_buf[total] = '\0';
    s_devlist_len = total;
}

/* ── GATT access callback ─────────────────────────────────────────── */

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const ble_uuid_t *chr_uuid = ctxt->chr->uuid;

    /* ── Read: Status characteristic ─────────────────────────────── */
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR &&
        ble_uuid_cmp(chr_uuid, &s_status_uuid.u) == 0) {

        if (s_status_mutex) {
            xSemaphoreTake(s_status_mutex, portMAX_DELAY);
        }
        int rc = os_mbuf_append(ctxt->om, s_status_buf, s_status_len);
        if (s_status_mutex) {
            xSemaphoreGive(s_status_mutex);
        }
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    /* ── Read: Device list characteristic ────────────────────────── */
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR &&
        ble_uuid_cmp(chr_uuid, &s_devlist_uuid.u) == 0) {

        rebuild_devlist();
        int rc = os_mbuf_append(ctxt->om, s_devlist_buf, s_devlist_len);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    /* ── Write: Command characteristic ───────────────────────────── */
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR &&
        ble_uuid_cmp(chr_uuid, &s_cmd_uuid.u) == 0) {

        /* Flatten the mbuf chain into a stack buffer */
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        if (om_len == 0 || om_len > 512) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        char cmd_buf[513];
        uint16_t copied = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, cmd_buf, om_len, &copied);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        cmd_buf[copied] = '\0';

        ESP_LOGI(TAG, "Cmd received (%u bytes): %s", copied, cmd_buf);

        /* Handle built-in familiar-device commands */
        handle_familiar_cmd(cmd_buf, copied);

        if (s_cmd_handler) {
            s_cmd_handler(cmd_buf, copied);
        }
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

/* ── GATT service definition ──────────────────────────────────────── */

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Alert Stream — notify only */
                .uuid       = &s_alert_uuid.u,
                .access_cb  = gatt_access_cb,
                .val_handle = &s_alert_handle,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
            },
            {
                /* Status — read + notify */
                .uuid       = &s_status_uuid.u,
                .access_cb  = gatt_access_cb,
                .val_handle = &s_status_handle,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                /* Device List — read only */
                .uuid       = &s_devlist_uuid.u,
                .access_cb  = gatt_access_cb,
                .val_handle = NULL,
                .flags      = BLE_GATT_CHR_F_READ,
            },
            {
                /* Command — write only */
                .uuid       = &s_cmd_uuid.u,
                .access_cb  = gatt_access_cb,
                .val_handle = NULL,
                .flags      = BLE_GATT_CHR_F_WRITE,
            },
            { 0 }, /* Sentinel */
        },
    },
    { 0 }, /* Sentinel */
};

/* ── GAP event callback ──────────────────────────────────────────── */

static void start_advertising(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Companion connected (handle=%d)", s_conn_handle);
        } else {
            ESP_LOGW(TAG, "Connection failed, status=%d", event->connect.status);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Companion disconnected (reason=%d)",
                 event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_alert_subscribed = false;
        s_status_subscribed = false;
        start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        /* Advertising timed out or was pre-empted; restart if idle */
        if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_alert_handle) {
            s_alert_subscribed = (event->subscribe.cur_notify != 0);
            ESP_LOGI(TAG, "Alert notifications %s",
                     s_alert_subscribed ? "enabled" : "disabled");
        } else if (event->subscribe.attr_handle == s_status_handle) {
            s_status_subscribed = (event->subscribe.cur_notify != 0);
            ESP_LOGI(TAG, "Status notifications %s",
                     s_status_subscribed ? "enabled" : "disabled");
        }
        break;

    default:
        break;
    }

    return 0;
}

/* ── Advertising ──────────────────────────────────────────────────── */

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,   /* Connectable */
        .disc_mode = BLE_GAP_DISC_MODE_GEN,   /* General discoverable */
        .itvl_min  = BLE_GAP_ADV_ITVL_MS(100),
        .itvl_max  = BLE_GAP_ADV_ITVL_MS(150),
    };

    /* Build advertisement data: flags + complete 128-bit service UUID */
    struct ble_hs_adv_fields adv_fields = { 0 };
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* Include the 128-bit service UUID in advertisement */
    adv_fields.uuids128 = (ble_uuid128_t[]){ s_svc_uuid };
    adv_fields.num_uuids128 = 1;
    adv_fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return;
    }

    /* Scan response: include device name */
    struct ble_hs_adv_fields rsp_fields = { 0 };
    const char *name = ble_svc_gap_device_name();
    rsp_fields.name = (uint8_t *)name;
    rsp_fields.name_len = strlen(name);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields failed: %d", rc);
        return;
    }

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                            &adv_params, gap_event_cb, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "Advertising started");
    } else if (rc == BLE_HS_EALREADY) {
        ESP_LOGD(TAG, "Already advertising");
    } else {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
    }
}

/* ── Public API ──────────────────────────────────────────────────── */

int ble_companion_init(void)
{
    s_status_mutex = xSemaphoreCreateMutex();
    if (!s_status_mutex) {
        ESP_LOGE(TAG, "Failed to create status mutex");
        return -1;
    }

    /* Set device name */
    int rc = ble_svc_gap_device_name_set("CYT-NG");
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_svc_gap_device_name_set failed: %d", rc);
    }

    /* Register mandatory GAP and GATT services */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* Register our custom service */
    rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return rc;
    }

    /* Seed status buffer with an initial empty status */
    s_status_len = (size_t)snprintf(s_status_buf, STATUS_BUF_SIZE,
        "{\"devs\":0,\"susp\":0,\"gps\":false,\"bat\":0,\"ses\":false}");

    ESP_LOGI(TAG, "BLE companion service registered");
    return 0;
}

void ble_companion_start_advertising(void)
{
    if (!ble_hs_synced()) {
        ESP_LOGW(TAG, "NimBLE host not synced, cannot advertise");
        return;
    }
    start_advertising();
}

void ble_companion_stop_advertising(void)
{
    int rc = ble_gap_adv_stop();
    if (rc == 0) {
        ESP_LOGI(TAG, "Advertising stopped");
    } else if (rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "ble_gap_adv_stop failed: %d", rc);
    }
}

void ble_companion_send_alert(companion_alert_level_t level,
                               const char *device_id,
                               float persistence_score,
                               uint8_t location_count)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_alert_subscribed) {
        return;
    }

    char json[512];
    int len = snprintf(json, sizeof(json),
        "{\"level\":%d,\"id\":\"%s\",\"score\":%.2f,\"locs\":%u}",
        (int)level,
        device_id ? device_id : "unknown",
        persistence_score,
        (unsigned)location_count);

    if (len <= 0 || (size_t)len >= sizeof(json)) {
        return;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(json, (uint16_t)len);
    if (!om) {
        ESP_LOGW(TAG, "Failed to allocate mbuf for alert");
        return;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_alert_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Alert notify failed: %d", rc);
    }
}

void ble_companion_update_status(const display_status_t *status,
                                  const gps_fix_t *gps)
{
    if (!status) {
        return;
    }

    char buf[STATUS_BUF_SIZE];
    int len = snprintf(buf, sizeof(buf),
        "{\"devs\":%lu,\"susp\":%lu,"
        "\"wifi\":%lu,\"ble\":%lu,\"tpms\":%lu,\"drone\":%lu,"
        "\"gps\":%s,\"lat\":%.6f,\"lon\":%.6f,"
        "\"bat\":%u,\"ses\":%s}",
        (unsigned long)status->total_devices,
        (unsigned long)status->suspicious_count,
        (unsigned long)status->wifi_count,
        (unsigned long)status->ble_count,
        (unsigned long)status->tpms_count,
        (unsigned long)status->drone_count,
        (gps && gps->has_fix) ? "true" : "false",
        gps ? gps->latitude : 0.0f,
        gps ? gps->longitude : 0.0f,
        (unsigned)status->battery_percent,
        status->session_active ? "true" : "false");

    if (len <= 0 || (size_t)len >= sizeof(buf)) {
        return;
    }

    /* Update cached buffer under lock */
    if (s_status_mutex) {
        xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    }
    memcpy(s_status_buf, buf, (size_t)len + 1);
    s_status_len = (size_t)len;
    if (s_status_mutex) {
        xSemaphoreGive(s_status_mutex);
    }

    /* Notify connected client if subscribed */
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_status_subscribed) {
        return;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, (uint16_t)len);
    if (!om) {
        ESP_LOGW(TAG, "Failed to allocate mbuf for status notify");
        return;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_status_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Status notify failed: %d", rc);
    }
}

bool ble_companion_is_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

void ble_companion_set_cmd_handler(companion_cmd_handler_t handler)
{
    s_cmd_handler = handler;
}
