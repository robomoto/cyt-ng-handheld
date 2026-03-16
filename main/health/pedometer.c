/**
 * Pedometer — QMI8658 accelerometer step counting.
 *
 * Reads the onboard QMI8658 IMU via I2C (GPIO 2 SDA, GPIO 3 SCL —
 * hardwired on the T-Display-S3 AMOLED).  Applies a low-pass filter to
 * the Z-axis acceleration and counts peaks above a threshold as steps.
 *
 * Daily totals are persisted to NVS before each midnight reset.
 */

#include "pedometer.h"

#include <string.h>
#include <math.h>
#include "esp_check.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2c.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "cyt_config.h"

static const char *TAG = "pedometer";

/* ── QMI8658 I2C parameters ─────────────────────────────────────── */
#define QMI8658_ADDR            0x6B
#define QMI8658_I2C_PORT        I2C_NUM_1
#define QMI8658_I2C_FREQ_HZ    400000

/* QMI8658 registers */
#define QMI8658_REG_WHO_AM_I    0x00
#define QMI8658_REG_CTRL1       0x02
#define QMI8658_REG_CTRL2       0x03
#define QMI8658_REG_CTRL7       0x08
#define QMI8658_REG_AX_L        0x35
#define QMI8658_WHO_AM_I_VAL    0x05

/* Accelerometer scale: ±4g → 1g = 8192 LSB */
#define ACCEL_SENSITIVITY       8192.0f

/* Step detection tunables */
#define STEP_THRESHOLD_G        1.2f
#define STEP_DEBOUNCE_MS        250
#define SAMPLE_RATE_HZ          50
#define SAMPLE_PERIOD_MS        (1000 / SAMPLE_RATE_HZ)
#define EMA_ALPHA               0.2f

/* NVS key names */
#define NVS_NAMESPACE           "pedometer"
#define NVS_KEY_STEPS_TOTAL     "steps_total"
#define NVS_KEY_STEPS_TODAY     "steps_today"

/* ── State ───────────────────────────────────────────────────────── */
static pedometer_stats_t s_stats;
static SemaphoreHandle_t s_mutex;
static float             s_weight_kg    = 70.0f;
static bool              s_initialised  = false;

/* ── Low-level I2C helpers ───────────────────────────────────────── */

static esp_err_t qmi_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(QMI8658_I2C_PORT, QMI8658_ADDR,
                                      buf, sizeof(buf),
                                      pdMS_TO_TICKS(50));
}

static esp_err_t qmi_read_reg(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_write_read_device(QMI8658_I2C_PORT, QMI8658_ADDR,
                                        &reg, 1, out, len,
                                        pdMS_TO_TICKS(50));
}

/* ── QMI8658 init ────────────────────────────────────────────────── */

static esp_err_t qmi8658_init_hw(void)
{
    /* I2C master config */
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = CYT_IMU_SDA_PIN,
        .scl_io_num       = CYT_IMU_SCL_PIN,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = QMI8658_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(QMI8658_I2C_PORT, &conf), TAG, "i2c cfg");
    ESP_RETURN_ON_ERROR(i2c_driver_install(QMI8658_I2C_PORT, I2C_MODE_MASTER,
                                           0, 0, 0), TAG, "i2c install");

    /* Verify WHO_AM_I */
    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(qmi_read_reg(QMI8658_REG_WHO_AM_I, &id, 1), TAG, "who_am_i");
    if (id != QMI8658_WHO_AM_I_VAL) {
        ESP_LOGE(TAG, "QMI8658 WHO_AM_I mismatch: 0x%02X (expected 0x%02X)", id, QMI8658_WHO_AM_I_VAL);
        return ESP_ERR_NOT_FOUND;
    }

    /* CTRL1: sensor enable */
    ESP_RETURN_ON_ERROR(qmi_write_reg(QMI8658_REG_CTRL1, 0x60), TAG, "ctrl1");
    /* CTRL2: accel ±4g, 50Hz ODR */
    ESP_RETURN_ON_ERROR(qmi_write_reg(QMI8658_REG_CTRL2, 0x15), TAG, "ctrl2");
    /* CTRL7: accel + gyro enable */
    ESP_RETURN_ON_ERROR(qmi_write_reg(QMI8658_REG_CTRL7, 0x03), TAG, "ctrl7");

    ESP_LOGI(TAG, "QMI8658 initialised (accel ±4g, 50Hz)");
    return ESP_OK;
}

/* ── Read acceleration (Z-axis in g) ─────────────────────────────── */

static float qmi8658_read_accel_z(void)
{
    /* Read 6 bytes starting at AX_L: AX_L, AX_H, AY_L, AY_H, AZ_L, AZ_H */
    uint8_t raw[6];
    if (qmi_read_reg(QMI8658_REG_AX_L, raw, 6) != ESP_OK) {
        return 0.0f;
    }
    int16_t az = (int16_t)((raw[5] << 8) | raw[4]);
    return (float)az / ACCEL_SENSITIVITY;
}

/* ── NVS helpers ─────────────────────────────────────────────────── */

static void nvs_save_stats(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, NVS_KEY_STEPS_TOTAL, s_stats.steps_total);
        nvs_set_u32(h, NVS_KEY_STEPS_TODAY, s_stats.steps_today);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void nvs_load_stats(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, NVS_KEY_STEPS_TOTAL, &s_stats.steps_total);
        nvs_get_u32(h, NVS_KEY_STEPS_TODAY, &s_stats.steps_today);
        nvs_close(h);
    }
}

/* ── Step detection task ─────────────────────────────────────────── */

static void pedometer_task(void *arg)
{
    (void)arg;

    float    filtered     = 1.0f;   /* Start at ~1g (gravity) */
    bool     was_above    = false;
    TickType_t last_step  = 0;

    const TickType_t period = pdMS_TO_TICKS(SAMPLE_PERIOD_MS);

    while (1) {
        float az = fabsf(qmi8658_read_accel_z());

        /* Exponential moving average low-pass filter */
        filtered = EMA_ALPHA * az + (1.0f - EMA_ALPHA) * filtered;

        bool is_above = (filtered > STEP_THRESHOLD_G);

        /* Rising-edge detection with debounce */
        if (is_above && !was_above) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_step) >= pdMS_TO_TICKS(STEP_DEBOUNCE_MS)) {
                last_step = now;

                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_stats.steps_today++;
                s_stats.steps_total++;
                s_stats.distance_km = (float)s_stats.steps_today *
                                      s_stats.stride_length_m / 1000.0f;
                s_stats.calories    = (float)s_stats.steps_today * 0.04f *
                                      (s_weight_kg / 70.0f);
                s_stats.active      = true;
                xSemaphoreGive(s_mutex);

                /* Persist every 50 steps */
                if ((s_stats.steps_today % 50) == 0) {
                    nvs_save_stats();
                }
            }
        }
        was_above = is_above;

        /* Clear "active" flag if no step in last 5 seconds */
        if (s_stats.active &&
            (xTaskGetTickCount() - last_step) > pdMS_TO_TICKS(5000)) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_stats.active = false;
            xSemaphoreGive(s_mutex);
        }

        vTaskDelay(period);
    }
}

/* ── Public API ──────────────────────────────────────────────────── */

int pedometer_init(void)
{
    if (s_initialised) return 0;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return -1;

    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.stride_length_m = 0.75f;

    nvs_load_stats();

    esp_err_t err = qmi8658_init_hw();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "QMI8658 init failed: %s", esp_err_to_name(err));
        return -1;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(pedometer_task, "pedometer",
                                            3072, NULL, 3, NULL, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create pedometer task");
        return -1;
    }

    s_initialised = true;
    ESP_LOGI(TAG, "Pedometer started (total steps: %lu)", (unsigned long)s_stats.steps_total);
    return 0;
}

pedometer_stats_t pedometer_get_stats(void)
{
    pedometer_stats_t copy;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    copy = s_stats;
    xSemaphoreGive(s_mutex);
    return copy;
}

void pedometer_reset_daily(void)
{
    /* Save today's totals before zeroing */
    nvs_save_stats();

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_stats.steps_today = 0;
    s_stats.distance_km = 0.0f;
    s_stats.calories    = 0.0f;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Daily counters reset");
}

void pedometer_set_stride(float meters)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_stats.stride_length_m = meters;
    xSemaphoreGive(s_mutex);
}

void pedometer_set_weight(float kg)
{
    s_weight_kg = kg;
}
