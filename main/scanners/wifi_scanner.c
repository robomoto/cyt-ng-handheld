/**
 * WiFi promiscuous mode scanner — captures probe requests.
 *
 * The promiscuous callback runs in WiFi driver context on Core 0 and must
 * complete in ~4 microseconds. It copies minimal fields onto the stack and
 * pushes to a FreeRTOS queue via xQueueSendFromISR. No logging, no malloc,
 * no PSRAM access, no mutexes.
 *
 * Channel hopping uses a weighted dwell strategy: 200ms on channels 1/6/11,
 * 100ms on all others.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_event.h"
#include "esp_log.h"

#include "wifi_scanner.h"
#include "../cyt_config.h"

/* ── Static state ──────────────────────────────────────────────────── */

static const char *TAG = "wifi_scan";

static QueueHandle_t     s_packet_queue  = NULL;
static TaskHandle_t      s_hop_task      = NULL;
static bool              s_wifi_inited   = false;

/* Weighted channel hop sequence: 1/6/11 appear frequently. */
static const uint8_t s_hop_channels[] = {
    1, 6, 11, 1, 6, 11, 2, 3, 4, 5, 1, 6, 11, 7, 8, 9, 10, 1, 6, 11, 12, 13
};
#define HOP_SEQ_LEN  (sizeof(s_hop_channels) / sizeof(s_hop_channels[0]))

/* ── Promiscuous callback (ISR context — KEEP FAST) ────────────────── */

static void IRAM_ATTR wifi_prom_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT) {
        return;
    }

    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *payload = pkt->payload;
    uint16_t len = pkt->rx_ctrl.sig_len;

    /* Minimum frame: 24-byte management header + 2 bytes (tag + length). */
    if (len < 26) {
        return;
    }

    /* Check subtype: probe request = 0x40 (subtype 4 in bits 7:4). */
    if ((payload[0] & 0xF0) != 0x40) {
        return;
    }

    /* Build packet info entirely on the stack. */
    wifi_packet_info_t info;

    /* Source MAC: bytes 10..15 of the management frame header. */
    info.src_mac[0] = payload[10];
    info.src_mac[1] = payload[11];
    info.src_mac[2] = payload[12];
    info.src_mac[3] = payload[13];
    info.src_mac[4] = payload[14];
    info.src_mac[5] = payload[15];

    /* SSID IE: tag ID at byte 24, length at byte 25, string at byte 26+. */
    uint8_t ssid_len = 0;
    if (payload[24] == 0x00) {  /* Tag 0 = SSID */
        ssid_len = payload[25];
        if (ssid_len > 32) {
            ssid_len = 32;
        }
        /* Bounds check against actual received length. */
        if ((uint16_t)(26 + ssid_len) > len) {
            ssid_len = 0;
        }
    }

    if (ssid_len > 0) {
        memcpy(info.ssid, &payload[26], ssid_len);
    }
    info.ssid[ssid_len] = '\0';
    info.ssid_len = ssid_len;

    info.rssi    = (int8_t)pkt->rx_ctrl.rssi;
    info.channel = (uint8_t)pkt->rx_ctrl.channel;

    /* Use uptime in seconds as timestamp (GPS timestamp set by analysis). */
    info.timestamp = (uint32_t)(xTaskGetTickCountFromISR() / configTICK_RATE_HZ);

    /* Push to queue — never block in ISR context. */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_packet_queue, &info, &xHigherPriorityTaskWoken);
    /* Yield if a higher-priority task was woken. */
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/* ── Channel hop task (Core 0) ─────────────────────────────────────── */

static void channel_hop_task(void *arg)
{
    uint8_t idx = 0;

    for (;;) {
        uint8_t ch = s_hop_channels[idx];

        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

        /* Primary channels (1/6/11) get longer dwell time. */
        TickType_t dwell;
        if (ch == 1 || ch == 6 || ch == 11) {
            dwell = pdMS_TO_TICKS(CYT_CHANNEL_DWELL_PRIMARY_MS);
        } else {
            dwell = pdMS_TO_TICKS(CYT_CHANNEL_DWELL_SECONDARY_MS);
        }
        vTaskDelay(dwell);

        idx++;
        if (idx >= HOP_SEQ_LEN) {
            idx = 0;
        }
    }
}

/* ── Public API ────────────────────────────────────────────────────── */

void wifi_scanner_start(void)
{
    /* Create the packet queue if it doesn't exist yet. */
    if (s_packet_queue == NULL) {
        s_packet_queue = xQueueCreate(CYT_PACKET_QUEUE_SIZE,
                                      sizeof(wifi_packet_info_t));
    }

    /* Initialize WiFi in NULL mode if not already done. */
    if (!s_wifi_inited) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
        ESP_ERROR_CHECK(esp_wifi_start());
        s_wifi_inited = true;
        ESP_LOGI(TAG, "WiFi initialized (NULL mode)");
    }

    /* Set promiscuous filter: management frames only. */
    wifi_promiscuous_filter_t filt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
    };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filt));

    /* Register callback and enable promiscuous mode. */
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(wifi_prom_cb));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    ESP_LOGI(TAG, "Promiscuous mode enabled");

    /* Create channel hop task pinned to Core 0. */
    if (s_hop_task == NULL) {
        xTaskCreatePinnedToCore(channel_hop_task, "ch_hop", 2048,
                                NULL, 6, &s_hop_task, 0);
        ESP_LOGI(TAG, "Channel hop task started (Core 0)");
    }
}

void wifi_scanner_stop(void)
{
    /* Disable promiscuous mode first to stop the callback. */
    esp_wifi_set_promiscuous(false);
    ESP_LOGI(TAG, "Promiscuous mode disabled");

    /* Delete channel hop task. */
    if (s_hop_task != NULL) {
        vTaskDelete(s_hop_task);
        s_hop_task = NULL;
        ESP_LOGI(TAG, "Channel hop task stopped");
    }
}

void *wifi_scanner_get_queue(void)
{
    /* Create the queue lazily so analysis_task can call this before start. */
    if (s_packet_queue == NULL) {
        s_packet_queue = xQueueCreate(CYT_PACKET_QUEUE_SIZE,
                                      sizeof(wifi_packet_info_t));
    }
    return (void *)s_packet_queue;
}
