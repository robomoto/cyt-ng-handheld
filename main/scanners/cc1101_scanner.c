/**
 * CC1101 sub-GHz scanner — SPI driver and TPMS/OOK packet decoding.
 *
 * Shares the SPI2_HOST bus with the SD card (MOSI=11, MISO=13, CLK=12).
 * CC1101 has its own CS (GPIO 9) and GDO0 IRQ (GPIO 8).
 */

#include "scanners/cc1101_scanner.h"
#include "cyt_config.h"

#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "cc1101";

/* ── CC1101 SPI command bytes ───────────────────────────────────── */
#define CC1101_WRITE        0x00
#define CC1101_WRITE_BURST  0x40
#define CC1101_READ         0x80
#define CC1101_READ_BURST   0xC0
#define CC1101_STATUS       0xC0

/* Strobe commands */
#define CC1101_SRES         0x30
#define CC1101_SFSTXON      0x31
#define CC1101_SXOFF        0x32
#define CC1101_SCAL         0x33
#define CC1101_SRX          0x34
#define CC1101_STX          0x35
#define CC1101_SIDLE        0x36
#define CC1101_SWOR         0x38
#define CC1101_SPWD         0x39
#define CC1101_SFRX         0x3A
#define CC1101_SFTX         0x3B
#define CC1101_SWORRST      0x3C
#define CC1101_SNOP         0x3D

/* Configuration registers */
#define CC1101_IOCFG2       0x00
#define CC1101_IOCFG1       0x01
#define CC1101_IOCFG0       0x02
#define CC1101_FIFOTHR      0x03
#define CC1101_SYNC1        0x04
#define CC1101_SYNC0        0x05
#define CC1101_PKTLEN       0x06
#define CC1101_PKTCTRL1     0x07
#define CC1101_PKTCTRL0     0x08
#define CC1101_ADDR         0x09
#define CC1101_CHANNR       0x0A
#define CC1101_FSCTRL1      0x0B
#define CC1101_FSCTRL0      0x0C
#define CC1101_FREQ2        0x0D
#define CC1101_FREQ1        0x0E
#define CC1101_FREQ0        0x0F
#define CC1101_MDMCFG4      0x10
#define CC1101_MDMCFG3      0x11
#define CC1101_MDMCFG2      0x12
#define CC1101_MDMCFG1      0x13
#define CC1101_MDMCFG0      0x14
#define CC1101_DEVIATN      0x15
#define CC1101_MCSM2        0x16
#define CC1101_MCSM1        0x17
#define CC1101_MCSM0        0x18
#define CC1101_FOCCFG       0x19
#define CC1101_BSCFG        0x1A
#define CC1101_AGCCTRL2     0x1B
#define CC1101_AGCCTRL1     0x1C
#define CC1101_AGCCTRL0     0x1D
#define CC1101_WOREVT1      0x1E
#define CC1101_WOREVT0      0x1F
#define CC1101_WORCTRL      0x20
#define CC1101_FREND1       0x21
#define CC1101_FREND0       0x22
#define CC1101_FSCAL3       0x23
#define CC1101_FSCAL2       0x24
#define CC1101_FSCAL1       0x25
#define CC1101_FSCAL0       0x26
#define CC1101_RCCTRL1      0x27
#define CC1101_RCCTRL0      0x28
#define CC1101_FSTEST       0x29
#define CC1101_PTEST        0x2A
#define CC1101_AGCTEST      0x2B
#define CC1101_TEST2        0x2C
#define CC1101_TEST1        0x2D
#define CC1101_TEST0        0x2E

/* Status registers (read with burst bit set) */
#define CC1101_PARTNUM      0x30
#define CC1101_VERSION      0x31
#define CC1101_FREQEST      0x32
#define CC1101_LQI          0x33
#define CC1101_RSSI         0x34
#define CC1101_MARCSTATE    0x35
#define CC1101_PKTSTATUS    0x38
#define CC1101_RXBYTES      0x3B
#define CC1101_TXBYTES      0x3A

/* FIFOs */
#define CC1101_RXFIFO       0x3F
#define CC1101_TXFIFO       0x3F

/* ── Known TPMS preamble patterns ───────────────────────────────── */

/*
 * Common TPMS encodings use Manchester or differential Manchester with
 * a preamble of alternating bits.  In raw OOK the preamble shows up as
 * 0xAA 0xAA ... or 0x55 0x55 ... before the sync word.
 *
 * Typical TPMS sync words:
 *   Schrader:     0xFF 0x19  (after preamble of 0xAA x5)
 *   Pacific/TRW:  0xFF 0x52  (after preamble of 0x55 x5)
 *   Generic OOK:  any repeated 0xAA sequence
 */
#define TPMS_PREAMBLE_AA    0xAA
#define TPMS_PREAMBLE_55    0x55
#define TPMS_MIN_PREAMBLE   3      /* Minimum preamble bytes to consider valid */
#define TPMS_PACKET_LEN     10     /* Typical TPMS payload length (bytes) */

/* ── Module state ───────────────────────────────────────────────── */

static spi_device_handle_t s_spi_dev;
static QueueHandle_t       s_detection_queue;
static TaskHandle_t        s_rx_task;
static SemaphoreHandle_t   s_spi_mutex;
static volatile bool       s_running;
static float               s_freq_mhz = 433.92f;

/* ── SPI low-level helpers ──────────────────────────────────────── */

/**
 * Single-byte SPI transfer: send one command/address byte, receive status.
 * Used for strobes and single register read/write.
 */
static uint8_t spi_transfer_byte(uint8_t addr, uint8_t data, bool write)
{
    spi_transaction_t txn = {
        .length    = 16,            /* 2 bytes = 16 bits */
        .tx_data   = { addr, data },
        .flags     = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
    };

    if (!write) {
        txn.tx_data[1] = 0x00;     /* Clock out zeros while reading */
    }

    esp_err_t err = spi_device_transmit(s_spi_dev, &txn);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI transfer failed: %s", esp_err_to_name(err));
        return 0;
    }
    return txn.rx_data[1];
}

static void cc1101_write_reg(uint8_t addr, uint8_t value)
{
    spi_transfer_byte(addr | CC1101_WRITE, value, true);
}

static uint8_t cc1101_read_reg(uint8_t addr)
{
    return spi_transfer_byte(addr | CC1101_READ, 0x00, false);
}

static uint8_t cc1101_read_status(uint8_t addr)
{
    return spi_transfer_byte(addr | CC1101_STATUS, 0x00, false);
}

static void cc1101_strobe(uint8_t strobe)
{
    spi_transaction_t txn = {
        .length  = 8,
        .tx_data = { strobe },
        .flags   = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
    };
    spi_device_transmit(s_spi_dev, &txn);
}

/**
 * Burst-read `len` bytes from the RX FIFO.
 */
static void cc1101_read_burst(uint8_t addr, uint8_t *buf, size_t len)
{
    uint8_t cmd = addr | CC1101_READ_BURST;

    spi_transaction_t txn = {
        .length    = (1 + len) * 8,
        .rxlength  = (1 + len) * 8,
        .tx_buffer = NULL,
        .rx_buffer = NULL,
        .flags     = 0,
    };

    /* Allocate DMA-capable buffers */
    uint8_t *tx = heap_caps_calloc(1, 1 + len, MALLOC_CAP_DMA);
    uint8_t *rx = heap_caps_calloc(1, 1 + len, MALLOC_CAP_DMA);
    if (!tx || !rx) {
        ESP_LOGE(TAG, "Burst read alloc failed");
        free(tx);
        free(rx);
        return;
    }

    tx[0] = cmd;
    txn.tx_buffer = tx;
    txn.rx_buffer = rx;

    esp_err_t err = spi_device_transmit(s_spi_dev, &txn);
    if (err == ESP_OK) {
        memcpy(buf, rx + 1, len);
    }

    free(tx);
    free(rx);
}

/* ── CC1101 register configuration ──────────────────────────────── */

/**
 * Compute FREQ2/1/0 register values for a given frequency.
 * f_carrier = (f_xosc / 2^16) * FREQ[23:0]
 * With 26 MHz crystal: FREQ = freq_hz * 2^16 / 26000000
 */
static void cc1101_set_freq_regs(float freq_mhz)
{
    uint32_t freq_word = (uint32_t)((freq_mhz * 1e6 * 65536.0) / 26e6 + 0.5);
    cc1101_write_reg(CC1101_FREQ2, (freq_word >> 16) & 0xFF);
    cc1101_write_reg(CC1101_FREQ1, (freq_word >> 8) & 0xFF);
    cc1101_write_reg(CC1101_FREQ0, freq_word & 0xFF);
    ESP_LOGI(TAG, "Frequency set to %.2f MHz (FREQ=0x%06lX)",
             freq_mhz, (unsigned long)freq_word);
}

/**
 * Full register configuration for OOK reception at the configured frequency.
 * Optimized for TPMS-like signals (~4.8 kbaud OOK, no sync word).
 */
static void cc1101_configure_regs(void)
{
    /* GDO2: assert on FIFO threshold (default) */
    cc1101_write_reg(CC1101_IOCFG2, 0x29);

    /* GDO0: assert when packet received or FIFO above threshold */
    cc1101_write_reg(CC1101_IOCFG0, 0x01);     /* Associated to RXFIFO threshold */

    /* FIFO threshold: 16 bytes in RX FIFO */
    cc1101_write_reg(CC1101_FIFOTHR, 0x07);

    /* Packet length (fixed at 32 for raw capture) */
    cc1101_write_reg(CC1101_PKTLEN, 0xFF);

    /* Packet control */
    cc1101_write_reg(CC1101_PKTCTRL1, 0x00);   /* No address check, no append status */
    cc1101_write_reg(CC1101_PKTCTRL0, 0x02);   /* Infinite packet length mode */

    /* Frequency synthesizer control */
    cc1101_write_reg(CC1101_FSCTRL1, 0x06);    /* IF frequency */
    cc1101_write_reg(CC1101_FSCTRL0, 0x00);

    /* Carrier frequency */
    cc1101_set_freq_regs(s_freq_mhz);

    /* Modem configuration — OOK, no sync, ~4.8 kbaud */
    cc1101_write_reg(CC1101_MDMCFG4, 0x87);    /* RX BW 325 kHz, DRATE_E=7 */
    cc1101_write_reg(CC1101_MDMCFG3, 0x32);    /* DRATE_M=50 → ~4.8 kbaud */
    cc1101_write_reg(CC1101_MDMCFG2, 0x32);    /* OOK modulation, no sync word */
    cc1101_write_reg(CC1101_MDMCFG1, 0x00);    /* No FEC, 2 preamble bytes */
    cc1101_write_reg(CC1101_MDMCFG0, 0xF8);    /* Channel spacing (default) */

    /* No deviation needed for OOK */
    cc1101_write_reg(CC1101_DEVIATN, 0x00);

    /* Main radio control state machine */
    cc1101_write_reg(CC1101_MCSM1, 0x3F);      /* Stay in RX after packet */
    cc1101_write_reg(CC1101_MCSM0, 0x18);      /* Auto-calibrate on idle→RX */

    /* Frequency offset compensation */
    cc1101_write_reg(CC1101_FOCCFG, 0x16);

    /* Bit sync */
    cc1101_write_reg(CC1101_BSCFG, 0x6C);

    /* AGC control — optimized for OOK */
    cc1101_write_reg(CC1101_AGCCTRL2, 0x43);   /* Max LNA gain, target 33 dB */
    cc1101_write_reg(CC1101_AGCCTRL1, 0x40);   /* Carrier sense relative threshold */
    cc1101_write_reg(CC1101_AGCCTRL0, 0x91);   /* Medium hysteresis, 16 samples */

    /* Front-end config for OOK */
    cc1101_write_reg(CC1101_FREND1, 0x56);
    cc1101_write_reg(CC1101_FREND0, 0x11);     /* PA table index 1 for OOK */

    /* Frequency synthesizer calibration */
    cc1101_write_reg(CC1101_FSCAL3, 0xE9);
    cc1101_write_reg(CC1101_FSCAL2, 0x2A);
    cc1101_write_reg(CC1101_FSCAL1, 0x00);
    cc1101_write_reg(CC1101_FSCAL0, 0x1F);

    /* Test registers */
    cc1101_write_reg(CC1101_TEST2, 0x81);
    cc1101_write_reg(CC1101_TEST1, 0x35);
    cc1101_write_reg(CC1101_TEST0, 0x09);
}

/* ── TPMS packet parsing ───────────────────────────────────────── */

/**
 * Convert raw CC1101 RSSI byte to dBm.
 * If RSSI_dec >= 128: RSSI_dBm = (RSSI_dec - 256) / 2 - RSSI_offset
 * If RSSI_dec < 128:  RSSI_dBm = RSSI_dec / 2 - RSSI_offset
 * RSSI_offset for CC1101 is typically 74 dB.
 */
static int8_t cc1101_rssi_to_dbm(uint8_t raw)
{
    int16_t rssi;
    if (raw >= 128) {
        rssi = (int16_t)(raw - 256) / 2 - 74;
    } else {
        rssi = (int16_t)raw / 2 - 74;
    }
    if (rssi < -128) rssi = -128;
    if (rssi > 0) rssi = 0;
    return (int8_t)rssi;
}

/**
 * Attempt to find a TPMS preamble in raw data and extract the sensor ID.
 *
 * TPMS sensors typically transmit:
 *   [preamble: AA AA AA AA AA] [sync: FF 19 or FF 52] [ID: 4 bytes] [data] [CRC]
 *
 * Returns true if a plausible TPMS packet was found.
 */
static bool try_decode_tpms(const uint8_t *data, size_t len,
                            cc1101_detection_t *out)
{
    if (len < TPMS_MIN_PREAMBLE + 2 + 4) {
        return false;
    }

    /* Scan for preamble of repeated 0xAA or 0x55 */
    for (size_t i = 0; i + TPMS_MIN_PREAMBLE + 2 + 4 <= len; i++) {
        int preamble_count = 0;
        uint8_t preamble_byte = data[i];

        if (preamble_byte != TPMS_PREAMBLE_AA &&
            preamble_byte != TPMS_PREAMBLE_55) {
            continue;
        }

        /* Count consecutive preamble bytes */
        for (size_t j = i; j < len && data[j] == preamble_byte; j++) {
            preamble_count++;
        }

        if (preamble_count < TPMS_MIN_PREAMBLE) {
            continue;
        }

        /* Look for sync word after preamble */
        size_t sync_pos = i + preamble_count;
        if (sync_pos + 2 + 4 > len) {
            continue;
        }

        uint8_t sync_hi = data[sync_pos];
        uint8_t sync_lo = data[sync_pos + 1];

        /* Accept known sync words or any 0xFF-prefixed sync */
        bool valid_sync = (sync_hi == 0xFF &&
                           (sync_lo == 0x19 || sync_lo == 0x52 ||
                            sync_lo == 0x56 || sync_lo == 0x01));

        /* Also accept raw OOK transition (no specific sync) after long preamble */
        if (!valid_sync && preamble_count < 5) {
            continue;
        }

        /* Extract 32-bit sensor ID from bytes after sync word */
        size_t id_pos = sync_pos + (valid_sync ? 2 : 0);
        if (id_pos + 4 > len) {
            continue;
        }

        uint32_t sensor_id = ((uint32_t)data[id_pos] << 24) |
                              ((uint32_t)data[id_pos + 1] << 16) |
                              ((uint32_t)data[id_pos + 2] << 8) |
                              (uint32_t)data[id_pos + 3];

        /* Reject all-zeros and all-ones IDs */
        if (sensor_id == 0x00000000 || sensor_id == 0xFFFFFFFF) {
            continue;
        }

        /* Populate the detection */
        memset(out, 0, sizeof(*out));
        out->raw_id = sensor_id;
        out->device_id[0] = (sensor_id >> 24) & 0xFF;
        out->device_id[1] = (sensor_id >> 16) & 0xFF;
        out->device_id[2] = (sensor_id >> 8) & 0xFF;
        out->device_id[3] = sensor_id & 0xFF;
        out->device_id[4] = 0;
        out->device_id[5] = 0;
        out->is_tpms = true;
        strncpy(out->protocol, "TPMS", sizeof(out->protocol) - 1);

        /*
         * Pressure and temperature decoding is protocol-specific and
         * varies widely.  For v1 we just log the raw ID; full decoding
         * can be added per protocol once we identify common formats in
         * the field.
         */
        out->pressure_psi = 0.0f;
        out->temperature_c = 0.0f;

        return true;
    }

    return false;
}

/**
 * Try to decode a generic OOK detection (non-TPMS).
 * Looks for any structured data with repeated patterns.
 */
static bool try_decode_ook(const uint8_t *data, size_t len,
                           cc1101_detection_t *out)
{
    if (len < 6) {
        return false;
    }

    /*
     * Heuristic: look for at least 2 consecutive non-zero, non-0xFF bytes
     * that could be a device ID.  Skip data that is all noise (random).
     */
    int non_trivial = 0;
    for (size_t i = 0; i < len; i++) {
        if (data[i] != 0x00 && data[i] != 0xFF &&
            data[i] != 0xAA && data[i] != 0x55) {
            non_trivial++;
        }
    }

    /* Need some structure to be interesting */
    if (non_trivial < 4) {
        return false;
    }

    /* Use first 4 non-trivial bytes as a pseudo-ID */
    memset(out, 0, sizeof(*out));
    size_t id_idx = 0;
    uint32_t raw_id = 0;
    for (size_t i = 0; i < len && id_idx < 4; i++) {
        if (data[i] != 0x00 && data[i] != 0xFF) {
            out->device_id[id_idx] = data[i];
            raw_id = (raw_id << 8) | data[i];
            id_idx++;
        }
    }

    out->raw_id = raw_id;
    out->is_tpms = false;
    strncpy(out->protocol, "ook_sensor", sizeof(out->protocol) - 1);
    out->pressure_psi = 0.0f;
    out->temperature_c = 0.0f;

    return true;
}

/* ── RX task ────────────────────────────────────────────────────── */

#define RX_BUF_SIZE     64
#define RX_POLL_MS      20

static void cc1101_rx_task(void *arg)
{
    uint8_t rx_buf[RX_BUF_SIZE];

    ESP_LOGI(TAG, "RX task started on core %d", xPortGetCoreID());

    while (s_running) {
        /* Poll GDO0 — high when FIFO above threshold */
        int gdo0 = gpio_get_level(CYT_CC1101_GDO0_PIN);
        if (!gdo0) {
            vTaskDelay(pdMS_TO_TICKS(RX_POLL_MS));
            continue;
        }

        xSemaphoreTake(s_spi_mutex, portMAX_DELAY);

        /* Read number of bytes in RX FIFO */
        uint8_t rx_bytes = cc1101_read_status(CC1101_RXBYTES);
        uint8_t overflow = rx_bytes & 0x80;
        rx_bytes &= 0x7F;

        if (overflow) {
            /* FIFO overflow — flush and restart */
            cc1101_strobe(CC1101_SIDLE);
            cc1101_strobe(CC1101_SFRX);
            cc1101_strobe(CC1101_SRX);
            xSemaphoreGive(s_spi_mutex);
            ESP_LOGW(TAG, "RX FIFO overflow, flushed");
            continue;
        }

        if (rx_bytes == 0) {
            xSemaphoreGive(s_spi_mutex);
            vTaskDelay(pdMS_TO_TICKS(RX_POLL_MS));
            continue;
        }

        /* Cap read to buffer size */
        if (rx_bytes > RX_BUF_SIZE) {
            rx_bytes = RX_BUF_SIZE;
        }

        /* Burst-read FIFO */
        cc1101_read_burst(CC1101_RXFIFO, rx_buf, rx_bytes);

        /* Read RSSI while still in RX */
        uint8_t rssi_raw = cc1101_read_status(CC1101_RSSI);
        int8_t rssi_dbm = cc1101_rssi_to_dbm(rssi_raw);

        xSemaphoreGive(s_spi_mutex);

        /* Attempt to decode the received data */
        cc1101_detection_t det;
        bool decoded = false;

        decoded = try_decode_tpms(rx_buf, rx_bytes, &det);
        if (!decoded) {
            decoded = try_decode_ook(rx_buf, rx_bytes, &det);
        }

        if (decoded) {
            det.rssi = rssi_dbm;
            det.timestamp = (uint32_t)(esp_timer_get_time() / 1000000ULL);

            if (xQueueSend(s_detection_queue, &det, 0) != pdTRUE) {
                ESP_LOGW(TAG, "Detection queue full, dropping packet");
            } else {
                ESP_LOGD(TAG, "Detection: %s ID=0x%08lX RSSI=%d",
                         det.protocol, (unsigned long)det.raw_id, det.rssi);
            }
        }
    }

    ESP_LOGI(TAG, "RX task exiting");
    vTaskDelete(NULL);
}

/* ── Public API ─────────────────────────────────────────────────── */

/* Flag shared with sd_logger to avoid double-initializing the SPI bus */
extern bool g_spi_bus_initialized;
bool g_spi_bus_initialized = false;

int cc1101_scanner_init(void)
{
    ESP_LOGI(TAG, "Initializing CC1101 scanner");

    /* Initialize SPI bus if not already done (SD card may have done it) */
    if (!g_spi_bus_initialized) {
        spi_bus_config_t bus_cfg = {
            .mosi_io_num     = CYT_SPI_MOSI_PIN,
            .miso_io_num     = CYT_SPI_MISO_PIN,
            .sclk_io_num     = CYT_SPI_CLK_PIN,
            .quadwp_io_num   = -1,
            .quadhd_io_num   = -1,
            .max_transfer_sz = 64,
        };

        esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
            return -1;
        }
        g_spi_bus_initialized = true;
        ESP_LOGI(TAG, "SPI2 bus initialized");
    }

    /* Add CC1101 as device on the shared bus */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 4 * 1000 * 1000,     /* 4 MHz — CC1101 max is 6.5 MHz */
        .mode           = 0,                     /* CPOL=0, CPHA=0 */
        .spics_io_num   = CYT_CC1101_CS_PIN,
        .queue_size     = 4,
        .flags          = 0,
    };

    esp_err_t err = spi_bus_add_device(SPI2_HOST, &dev_cfg, &s_spi_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI add device failed: %s", esp_err_to_name(err));
        return -1;
    }

    /* Configure GDO0 as input */
    gpio_config_t gdo0_cfg = {
        .pin_bit_mask = (1ULL << CYT_CC1101_GDO0_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&gdo0_cfg);

    /* SPI mutex for shared bus access */
    s_spi_mutex = xSemaphoreCreateMutex();
    if (!s_spi_mutex) {
        ESP_LOGE(TAG, "Failed to create SPI mutex");
        return -1;
    }

    /* Detection queue */
    s_detection_queue = xQueueCreate(CYT_PACKET_QUEUE_SIZE,
                                     sizeof(cc1101_detection_t));
    if (!s_detection_queue) {
        ESP_LOGE(TAG, "Failed to create detection queue");
        return -1;
    }

    /* Reset CC1101 */
    xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
    cc1101_strobe(CC1101_SRES);
    xSemaphoreGive(s_spi_mutex);

    /* Wait for reset to complete */
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Verify CC1101 is responding — read part number */
    xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
    uint8_t partnum = cc1101_read_status(CC1101_PARTNUM);
    uint8_t version = cc1101_read_status(CC1101_VERSION);
    xSemaphoreGive(s_spi_mutex);

    ESP_LOGI(TAG, "CC1101 part=0x%02X version=0x%02X", partnum, version);

    if (version == 0x00 || version == 0xFF) {
        ESP_LOGE(TAG, "CC1101 not detected — check SPI wiring");
        return -1;
    }

    /* Configure registers for OOK reception */
    xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
    cc1101_configure_regs();
    xSemaphoreGive(s_spi_mutex);

    /* Calibrate */
    xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
    cc1101_strobe(CC1101_SCAL);
    xSemaphoreGive(s_spi_mutex);
    vTaskDelay(pdMS_TO_TICKS(5));

    ESP_LOGI(TAG, "CC1101 initialized at %.2f MHz", s_freq_mhz);
    return 0;
}

void cc1101_scanner_start(void)
{
    if (s_running) {
        ESP_LOGW(TAG, "Scanner already running");
        return;
    }

    ESP_LOGI(TAG, "Starting CC1101 scanner");
    s_running = true;

    /* Enter RX mode */
    xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
    cc1101_strobe(CC1101_SFRX);     /* Flush RX FIFO first */
    cc1101_strobe(CC1101_SRX);
    xSemaphoreGive(s_spi_mutex);

    /* Create RX polling task on Core 0 (Core 1 reserved for analysis) */
    xTaskCreatePinnedToCore(cc1101_rx_task, "cc1101_rx", 4096, NULL,
                            5, &s_rx_task, 0);
}

void cc1101_scanner_stop(void)
{
    if (!s_running) {
        return;
    }

    ESP_LOGI(TAG, "Stopping CC1101 scanner");
    s_running = false;

    /* Wait for task to exit */
    vTaskDelay(pdMS_TO_TICKS(RX_POLL_MS * 3));

    /* Go to idle */
    xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
    cc1101_strobe(CC1101_SIDLE);
    xSemaphoreGive(s_spi_mutex);

    s_rx_task = NULL;
}

void cc1101_scanner_set_frequency(float freq_mhz)
{
    ESP_LOGI(TAG, "Setting frequency to %.2f MHz", freq_mhz);
    s_freq_mhz = freq_mhz;

    bool was_running = s_running;
    if (was_running) {
        cc1101_scanner_stop();
    }

    xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
    cc1101_strobe(CC1101_SIDLE);
    cc1101_set_freq_regs(freq_mhz);
    cc1101_strobe(CC1101_SCAL);
    xSemaphoreGive(s_spi_mutex);

    vTaskDelay(pdMS_TO_TICKS(5));

    if (was_running) {
        cc1101_scanner_start();
    }
}

void *cc1101_scanner_get_queue(void)
{
    return (void *)s_detection_queue;
}
