/**
 * TFT display driver — ST7789 170x320 on LilyGo T-Display-S3.
 *
 * Uses the ESP-IDF v5.x esp_lcd component with esp_lcd_panel_io_spi
 * and esp_lcd_new_panel_st7789 for the built-in display.
 *
 * Text-only rendering in v1 using a minimal 8x16 bitmap font.
 */

#include "ui/display.h"
#include "cyt_config.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "display";

/* ── T-Display-S3 pin definitions ───────────────────────────────── */

/*
 * The T-Display-S3 (non-touch) uses a parallel 8-bit interface by default,
 * but the ST7789 can also be driven via SPI.  LilyGo's schematic shows:
 *
 * For the built-in display on T-Display-S3:
 *   - Uses 8-bit parallel (Intel 8080) interface, not SPI
 *   - But esp_lcd supports both; we use the 8080 interface here
 *
 * Pin assignments from the T-Display-S3 schematic:
 */
#define DISPLAY_PIN_RD      9   /* Not used in write-only mode — set high */
#define DISPLAY_PIN_WR      8
#define DISPLAY_PIN_RS      7   /* DC (data/command) */
#define DISPLAY_PIN_CS      6
#define DISPLAY_PIN_RST     5
#define DISPLAY_PIN_BL      38  /* Backlight PWM */

/* 8-bit parallel data bus */
#define DISPLAY_PIN_D0      39
#define DISPLAY_PIN_D1      40
#define DISPLAY_PIN_D2      41
#define DISPLAY_PIN_D3      42
#define DISPLAY_PIN_D4      45
#define DISPLAY_PIN_D5      46
#define DISPLAY_PIN_D6      47
#define DISPLAY_PIN_D7      48

/* ── Display constants ──────────────────────────────────────────── */

#define DISPLAY_W           CYT_DISPLAY_WIDTH   /* 170 */
#define DISPLAY_H           CYT_DISPLAY_HEIGHT  /* 320 */
#define FONT_W              8
#define FONT_H              16
#define CHARS_PER_LINE      (DISPLAY_W / FONT_W)    /* 21 chars */
#define LINES_PER_SCREEN    (DISPLAY_H / FONT_H)    /* 20 lines */

/* Colors — RGB565 */
#define COLOR_BLACK         0x0000
#define COLOR_WHITE         0xFFFF
#define COLOR_GREEN         0x07E0
#define COLOR_RED           0xF800
#define COLOR_YELLOW        0xFFE0
#define COLOR_CYAN          0x07FF
#define COLOR_ORANGE        0xFD20
#define COLOR_DARK_GRAY     0x2104

/* ── Minimal 8x16 bitmap font ──────────────────────────────────── */

/*
 * Subset of CP437 8x16 font covering ASCII 0x20-0x7E (printable).
 * Each character is 16 bytes (one byte per row, 8 pixels wide).
 * Full font data is large (~1.5 KB); here we include the essential
 * characters and use a simple fallback for missing ones.
 *
 * In a production build this would be a complete font table in flash.
 * For v1 we generate characters procedurally from a compressed
 * representation — but to keep this compilable, we use a lookup stub
 * that returns basic character shapes.
 */

/**
 * Get font bitmap for a character (16 bytes, one per row).
 * Returns pointer to static data — valid until next call.
 */
static const uint8_t *font_get_char(char c)
{
    /*
     * Minimal procedural font: generates recognizable glyphs for
     * digits 0-9, uppercase A-Z, and basic punctuation.
     *
     * Each row is 8 bits wide.  Bit 7 = leftmost pixel.
     */
    static uint8_t glyph[16];
    memset(glyph, 0x00, sizeof(glyph));

    if (c < 0x20 || c > 0x7E) {
        /* Non-printable: render as filled block */
        memset(glyph, 0xFF, sizeof(glyph));
        return glyph;
    }

    if (c == ' ') {
        return glyph;  /* All zeros — blank */
    }

    /*
     * For v1 we use a very simple block-letter approach.
     * Each character is rendered as a recognizable shape in an 8x16 grid.
     * This is crude but readable on the small display.
     */
    switch (c) {
    case '0':
        glyph[2]=0x3C; glyph[3]=0x66; glyph[4]=0x6E; glyph[5]=0x76;
        glyph[6]=0x66; glyph[7]=0x66; glyph[8]=0x66; glyph[9]=0x66;
        glyph[10]=0x66; glyph[11]=0x3C;
        break;
    case '1':
        glyph[2]=0x18; glyph[3]=0x38; glyph[4]=0x18; glyph[5]=0x18;
        glyph[6]=0x18; glyph[7]=0x18; glyph[8]=0x18; glyph[9]=0x18;
        glyph[10]=0x18; glyph[11]=0x7E;
        break;
    case '2':
        glyph[2]=0x3C; glyph[3]=0x66; glyph[4]=0x06; glyph[5]=0x06;
        glyph[6]=0x0C; glyph[7]=0x18; glyph[8]=0x30; glyph[9]=0x60;
        glyph[10]=0x66; glyph[11]=0x7E;
        break;
    case '3':
        glyph[2]=0x3C; glyph[3]=0x66; glyph[4]=0x06; glyph[5]=0x06;
        glyph[6]=0x1C; glyph[7]=0x06; glyph[8]=0x06; glyph[9]=0x06;
        glyph[10]=0x66; glyph[11]=0x3C;
        break;
    case '4':
        glyph[2]=0x0C; glyph[3]=0x1C; glyph[4]=0x2C; glyph[5]=0x4C;
        glyph[6]=0x7E; glyph[7]=0x0C; glyph[8]=0x0C; glyph[9]=0x0C;
        glyph[10]=0x0C; glyph[11]=0x0C;
        break;
    case '5':
        glyph[2]=0x7E; glyph[3]=0x60; glyph[4]=0x60; glyph[5]=0x7C;
        glyph[6]=0x06; glyph[7]=0x06; glyph[8]=0x06; glyph[9]=0x06;
        glyph[10]=0x66; glyph[11]=0x3C;
        break;
    case '6':
        glyph[2]=0x3C; glyph[3]=0x66; glyph[4]=0x60; glyph[5]=0x7C;
        glyph[6]=0x66; glyph[7]=0x66; glyph[8]=0x66; glyph[9]=0x66;
        glyph[10]=0x66; glyph[11]=0x3C;
        break;
    case '7':
        glyph[2]=0x7E; glyph[3]=0x06; glyph[4]=0x06; glyph[5]=0x0C;
        glyph[6]=0x18; glyph[7]=0x18; glyph[8]=0x18; glyph[9]=0x18;
        glyph[10]=0x18; glyph[11]=0x18;
        break;
    case '8':
        glyph[2]=0x3C; glyph[3]=0x66; glyph[4]=0x66; glyph[5]=0x66;
        glyph[6]=0x3C; glyph[7]=0x66; glyph[8]=0x66; glyph[9]=0x66;
        glyph[10]=0x66; glyph[11]=0x3C;
        break;
    case '9':
        glyph[2]=0x3C; glyph[3]=0x66; glyph[4]=0x66; glyph[5]=0x66;
        glyph[6]=0x3E; glyph[7]=0x06; glyph[8]=0x06; glyph[9]=0x06;
        glyph[10]=0x66; glyph[11]=0x3C;
        break;
    case ':':
        glyph[5]=0x18; glyph[6]=0x18; glyph[9]=0x18; glyph[10]=0x18;
        break;
    case '.':
        glyph[10]=0x18; glyph[11]=0x18;
        break;
    case ',':
        glyph[10]=0x18; glyph[11]=0x18; glyph[12]=0x08; glyph[13]=0x10;
        break;
    case '-':
        glyph[7]=0x7E;
        break;
    case '/':
        glyph[2]=0x02; glyph[3]=0x04; glyph[4]=0x04; glyph[5]=0x08;
        glyph[6]=0x10; glyph[7]=0x10; glyph[8]=0x20; glyph[9]=0x20;
        glyph[10]=0x40; glyph[11]=0x40;
        break;
    case '%':
        glyph[2]=0x62; glyph[3]=0x64; glyph[4]=0x08; glyph[5]=0x10;
        glyph[6]=0x10; glyph[7]=0x20; glyph[8]=0x26; glyph[9]=0x46;
        break;
    case '!':
        glyph[2]=0x18; glyph[3]=0x18; glyph[4]=0x18; glyph[5]=0x18;
        glyph[6]=0x18; glyph[7]=0x18; glyph[8]=0x00; glyph[9]=0x00;
        glyph[10]=0x18; glyph[11]=0x18;
        break;
    case '(':
        glyph[2]=0x0C; glyph[3]=0x18; glyph[4]=0x30; glyph[5]=0x30;
        glyph[6]=0x30; glyph[7]=0x30; glyph[8]=0x30; glyph[9]=0x30;
        glyph[10]=0x18; glyph[11]=0x0C;
        break;
    case ')':
        glyph[2]=0x30; glyph[3]=0x18; glyph[4]=0x0C; glyph[5]=0x0C;
        glyph[6]=0x0C; glyph[7]=0x0C; glyph[8]=0x0C; glyph[9]=0x0C;
        glyph[10]=0x18; glyph[11]=0x30;
        break;
    default:
        /* For letters and other chars, generate a simple block representation */
        if (c >= 'A' && c <= 'Z') {
            /* Uppercase letters — simplified bitmaps */
            static const uint8_t letters[][10] = {
                /* A */ {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x66,0x66,0x00},
                /* B */ {0x7C,0x66,0x66,0x7C,0x66,0x66,0x66,0x7C,0x00,0x00},
                /* C */ {0x3C,0x66,0x60,0x60,0x60,0x60,0x66,0x3C,0x00,0x00},
                /* D */ {0x78,0x6C,0x66,0x66,0x66,0x66,0x6C,0x78,0x00,0x00},
                /* E */ {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x7E,0x00,0x00},
                /* F */ {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x60,0x00,0x00},
                /* G */ {0x3C,0x66,0x60,0x60,0x6E,0x66,0x66,0x3E,0x00,0x00},
                /* H */ {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x66,0x00,0x00},
                /* I */ {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00},
                /* J */ {0x3E,0x06,0x06,0x06,0x06,0x06,0x66,0x3C,0x00,0x00},
                /* K */ {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x66,0x00,0x00},
                /* L */ {0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00,0x00},
                /* M */ {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x63,0x00,0x00},
                /* N */ {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x66,0x00,0x00},
                /* O */ {0x3C,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00},
                /* P */ {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0x00,0x00},
                /* Q */ {0x3C,0x66,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00,0x00},
                /* R */ {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0x00,0x00},
                /* S */ {0x3C,0x66,0x60,0x3C,0x06,0x06,0x66,0x3C,0x00,0x00},
                /* T */ {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00},
                /* U */ {0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00},
                /* V */ {0x66,0x66,0x66,0x66,0x66,0x3C,0x3C,0x18,0x00,0x00},
                /* W */ {0x63,0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00,0x00},
                /* X */ {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x66,0x00,0x00},
                /* Y */ {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x00,0x00},
                /* Z */ {0x7E,0x06,0x0C,0x18,0x30,0x60,0x60,0x7E,0x00,0x00},
            };
            int idx = c - 'A';
            for (int i = 0; i < 10; i++) {
                glyph[i + 3] = letters[idx][i];
            }
        } else if (c >= 'a' && c <= 'z') {
            /* Lowercase: reuse uppercase shifted down slightly */
            return font_get_char(c - 32);
        } else if (c >= '0' && c <= '9') {
            /* Already handled above */
        } else {
            /* Unknown: small dot */
            glyph[7] = 0x18;
            glyph[8] = 0x18;
        }
        break;
    }

    return glyph;
}

/* ── Module state ───────────────────────────────────────────────── */

static esp_lcd_panel_handle_t  s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static screen_id_t             s_current_screen = SCREEN_STATUS;
static display_status_t        s_last_status;
static SemaphoreHandle_t       s_disp_mutex;
static int64_t                 s_last_activity_us;
static bool                    s_backlight_on;

/* Framebuffer line — one row of pixels in RGB565, used for drawing */
static uint16_t s_line_buf[DISPLAY_W];

/* ── Backlight control via LEDC PWM ─────────────────────────────── */

static void backlight_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = DISPLAY_PIN_BL,
        .duty       = 200,          /* ~80% brightness */
        .hpoint     = 0,
    };
    ledc_channel_config(&ch_cfg);

    s_backlight_on = true;
}

static void backlight_set(bool on)
{
    uint32_t duty = on ? 200 : 0;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    s_backlight_on = on;
}

/* ── Drawing primitives ─────────────────────────────────────────── */

/**
 * Fill a rectangular region with a solid color.
 */
static void draw_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w > DISPLAY_W) w = DISPLAY_W;

    for (int i = 0; i < w; i++) {
        s_line_buf[i] = color;
    }

    for (int row = y; row < y + h && row < DISPLAY_H; row++) {
        esp_lcd_panel_draw_bitmap(s_panel, x, row, x + w, row + 1, s_line_buf);
    }
}

/**
 * Draw a single character at pixel position (x, y) with foreground/background colors.
 */
static void draw_char(int x, int y, char c, uint16_t fg, uint16_t bg)
{
    const uint8_t *bitmap = font_get_char(c);

    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = bitmap[row];
        for (int col = 0; col < FONT_W; col++) {
            s_line_buf[col] = (bits & (0x80 >> col)) ? fg : bg;
        }
        int py = y + row;
        if (py >= 0 && py < DISPLAY_H) {
            esp_lcd_panel_draw_bitmap(s_panel, x, py, x + FONT_W, py + 1,
                                      s_line_buf);
        }
    }
}

/**
 * Draw a string at character grid position (col, line).
 * Truncates at screen edge.
 */
static void draw_text(int col, int line, const char *text,
                      uint16_t fg, uint16_t bg)
{
    int x = col * FONT_W;
    int y = line * FONT_H;

    for (int i = 0; text[i] && (col + i) < CHARS_PER_LINE; i++) {
        draw_char(x + i * FONT_W, y, text[i], fg, bg);
    }
}

/**
 * Draw a string and pad the rest of the line with background color.
 * Ensures old text is overwritten when lines get shorter.
 */
static void draw_text_line(int line, const char *text,
                           uint16_t fg, uint16_t bg)
{
    char padded[CHARS_PER_LINE + 1];
    int len = strlen(text);
    if (len > CHARS_PER_LINE) len = CHARS_PER_LINE;
    memcpy(padded, text, len);
    memset(padded + len, ' ', CHARS_PER_LINE - len);
    padded[CHARS_PER_LINE] = '\0';

    draw_text(0, line, padded, fg, bg);
}

/**
 * Clear the entire screen to a color.
 */
static void draw_clear(uint16_t color)
{
    draw_fill_rect(0, 0, DISPLAY_W, DISPLAY_H, color);
}

/* ── Screen renderers ───────────────────────────────────────────── */

static void render_status_screen(const display_status_t *st)
{
    char buf[CHARS_PER_LINE + 1];

    /* Title bar */
    draw_text_line(0, "  CYT-NG HANDHELD", COLOR_BLACK, COLOR_CYAN);

    /* Blank separator */
    draw_text_line(1, "", COLOR_WHITE, COLOR_BLACK);

    /* Device counts */
    snprintf(buf, sizeof(buf), "DEVICES: %lu", (unsigned long)st->total_devices);
    draw_text_line(2, buf, COLOR_WHITE, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "WIFI: %lu  BLE: %lu",
             (unsigned long)st->wifi_count, (unsigned long)st->ble_count);
    draw_text_line(3, buf, COLOR_GREEN, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "TPMS: %lu  DRONE: %lu",
             (unsigned long)st->tpms_count, (unsigned long)st->drone_count);
    draw_text_line(4, buf, COLOR_GREEN, COLOR_BLACK);

    draw_text_line(5, "", COLOR_WHITE, COLOR_BLACK);

    /* Alert count */
    uint16_t alert_color = st->suspicious_count > 0 ? COLOR_RED : COLOR_GREEN;
    snprintf(buf, sizeof(buf), "ALERTS: %lu", (unsigned long)st->suspicious_count);
    draw_text_line(6, buf, alert_color, COLOR_BLACK);

    draw_text_line(7, "", COLOR_WHITE, COLOR_BLACK);

    /* GPS status */
    snprintf(buf, sizeof(buf), "GPS: %s", st->gps_fix ? "FIX OK" : "NO FIX");
    draw_text_line(8, buf, st->gps_fix ? COLOR_GREEN : COLOR_YELLOW, COLOR_BLACK);

    /* Battery */
    snprintf(buf, sizeof(buf), "BATT: %u%%", st->battery_percent);
    uint16_t batt_color = st->battery_percent > 20 ? COLOR_GREEN : COLOR_RED;
    draw_text_line(9, buf, batt_color, COLOR_BLACK);

    /* SD status */
    snprintf(buf, sizeof(buf), "SD: %s",
             st->sd_ready ? (st->session_active ? "LOGGING" : "READY") : "NONE");
    uint16_t sd_color = st->sd_ready ? COLOR_GREEN : COLOR_RED;
    draw_text_line(10, buf, sd_color, COLOR_BLACK);

    /* Clear remaining lines */
    for (int i = 11; i < LINES_PER_SCREEN; i++) {
        draw_text_line(i, "", COLOR_WHITE, COLOR_BLACK);
    }
}

static void render_alert_screen(const display_status_t *st)
{
    char buf[CHARS_PER_LINE + 1];

    /* Title bar */
    draw_text_line(0, "   ALERT DETAIL", COLOR_BLACK, COLOR_RED);

    draw_text_line(1, "", COLOR_WHITE, COLOR_BLACK);

    if (st->suspicious_count == 0) {
        draw_text_line(3, "  NO ALERTS", COLOR_GREEN, COLOR_BLACK);
        draw_text_line(5, " ALL CLEAR", COLOR_GREEN, COLOR_BLACK);

        for (int i = 6; i < LINES_PER_SCREEN; i++) {
            draw_text_line(i, "", COLOR_WHITE, COLOR_BLACK);
        }
        return;
    }

    draw_text_line(2, "HIGHEST PERSIST:", COLOR_YELLOW, COLOR_BLACK);

    /* Device ID */
    snprintf(buf, sizeof(buf), "ID: %.18s", st->highest_device_id);
    draw_text_line(4, buf, COLOR_WHITE, COLOR_BLACK);

    /* Persistence score */
    snprintf(buf, sizeof(buf), "SCORE: %.2f", st->highest_persistence);
    uint16_t score_color = st->highest_persistence >= 0.7f ? COLOR_RED : COLOR_ORANGE;
    draw_text_line(6, buf, score_color, COLOR_BLACK);

    draw_text_line(7, "", COLOR_WHITE, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "TOTAL ALERTS: %lu",
             (unsigned long)st->suspicious_count);
    draw_text_line(8, buf, COLOR_RED, COLOR_BLACK);

    draw_text_line(10, "CHECK DEVICE LIST", COLOR_YELLOW, COLOR_BLACK);
    draw_text_line(11, "FOR MORE DETAILS", COLOR_YELLOW, COLOR_BLACK);

    for (int i = 12; i < LINES_PER_SCREEN; i++) {
        draw_text_line(i, "", COLOR_WHITE, COLOR_BLACK);
    }
}

static void render_devices_screen(const display_status_t *st)
{
    char buf[CHARS_PER_LINE + 1];

    /* Title bar */
    draw_text_line(0, "  DEVICE LIST", COLOR_BLACK, COLOR_GREEN);

    draw_text_line(1, "", COLOR_WHITE, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "%lu TOTAL TRACKED",
             (unsigned long)st->total_devices);
    draw_text_line(2, buf, COLOR_WHITE, COLOR_BLACK);

    draw_text_line(3, "", COLOR_WHITE, COLOR_BLACK);

    /*
     * In v1 we show summary counts only.  A full scrollable device list
     * requires the display task to query the device table directly,
     * which will be added in v2 with proper page-up/page-down support.
     */
    snprintf(buf, sizeof(buf), "WIFI:  %lu", (unsigned long)st->wifi_count);
    draw_text_line(4, buf, COLOR_CYAN, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "BLE:   %lu", (unsigned long)st->ble_count);
    draw_text_line(5, buf, COLOR_CYAN, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "TPMS:  %lu", (unsigned long)st->tpms_count);
    draw_text_line(6, buf, COLOR_CYAN, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "DRONE: %lu", (unsigned long)st->drone_count);
    draw_text_line(7, buf, COLOR_CYAN, COLOR_BLACK);

    draw_text_line(8, "", COLOR_WHITE, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "SUSPICIOUS: %lu",
             (unsigned long)st->suspicious_count);
    uint16_t color = st->suspicious_count > 0 ? COLOR_RED : COLOR_GREEN;
    draw_text_line(9, buf, color, COLOR_BLACK);

    draw_text_line(10, "", COLOR_WHITE, COLOR_BLACK);

    /* Navigation hint */
    draw_text_line(18, "UP/DOWN: SCROLL", COLOR_DARK_GRAY, COLOR_BLACK);
    draw_text_line(19, "MODE: NEXT SCREEN", COLOR_DARK_GRAY, COLOR_BLACK);

    /* Clear middle lines */
    for (int i = 11; i < 18; i++) {
        draw_text_line(i, "", COLOR_WHITE, COLOR_BLACK);
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing ST7789 display (170x320)");

    s_disp_mutex = xSemaphoreCreateMutex();

    /* Initialize backlight PWM */
    backlight_init();

    /* Reset pin */
    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << DISPLAY_PIN_RST),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_cfg);

    /* Hardware reset */
    gpio_set_level(DISPLAY_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(DISPLAY_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    /*
     * Configure the 8080 parallel interface for the T-Display-S3.
     * ESP-IDF esp_lcd provides an i80 bus abstraction for this.
     */
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_cfg = {
        .clk_src      = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num  = DISPLAY_PIN_RS,
        .wr_gpio_num  = DISPLAY_PIN_WR,
        .data_gpio_nums = {
            DISPLAY_PIN_D0, DISPLAY_PIN_D1, DISPLAY_PIN_D2, DISPLAY_PIN_D3,
            DISPLAY_PIN_D4, DISPLAY_PIN_D5, DISPLAY_PIN_D6, DISPLAY_PIN_D7,
        },
        .bus_width          = 8,
        .max_transfer_bytes = DISPLAY_W * FONT_H * sizeof(uint16_t),
        .psram_trans_align  = 64,
        .sram_trans_align   = 4,
    };

    esp_err_t err = esp_lcd_new_i80_bus(&bus_cfg, &i80_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create i80 bus: %s", esp_err_to_name(err));
        return;
    }

    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num       = DISPLAY_PIN_CS,
        .pclk_hz           = 10 * 1000 * 1000,  /* 10 MHz pixel clock */
        .trans_queue_depth  = 10,
        .dc_levels = {
            .dc_idle_level  = 0,
            .dc_cmd_level   = 0,
            .dc_dummy_level = 0,
            .dc_data_level  = 1,
        },
        .lcd_cmd_bits   = 8,
        .lcd_param_bits = 8,
    };

    err = esp_lcd_new_panel_io_i80(i80_bus, &io_cfg, &s_panel_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(err));
        return;
    }

    /* Create the ST7789 panel driver */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = DISPLAY_PIN_RST,
        .rgb_endian     = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };

    err = esp_lcd_new_panel_st7789(s_panel_io, &panel_cfg, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ST7789 panel: %s", esp_err_to_name(err));
        return;
    }

    /* Initialize the panel */
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);

    /*
     * T-Display-S3 display orientation:
     * The 170x320 display is mounted in portrait.  Swap XY and mirror
     * as needed to get the correct orientation for handheld use.
     */
    esp_lcd_panel_swap_xy(s_panel, true);
    esp_lcd_panel_mirror(s_panel, false, true);
    esp_lcd_panel_invert_color(s_panel, true);  /* ST7789 needs color inversion */

    /* Turn on the display */
    esp_lcd_panel_disp_on_off(s_panel, true);

    /* Clear to black */
    draw_clear(COLOR_BLACK);

    /* Show boot message */
    draw_text_line(4, "  CYT-NG v1.0", COLOR_CYAN, COLOR_BLACK);
    draw_text_line(6, "  INITIALIZING...", COLOR_WHITE, COLOR_BLACK);

    s_last_activity_us = esp_timer_get_time();
    memset(&s_last_status, 0, sizeof(s_last_status));

    ESP_LOGI(TAG, "Display initialized");
}

void display_update(const display_status_t *status)
{
    if (!s_panel || !status) {
        return;
    }

    xSemaphoreTake(s_disp_mutex, portMAX_DELAY);

    /* Copy status for use by renderers */
    memcpy(&s_last_status, status, sizeof(s_last_status));

    /* Check auto-off timer */
    int64_t now = esp_timer_get_time();
    int64_t idle_ms = (now - s_last_activity_us) / 1000;

    if (s_backlight_on && idle_ms > CYT_DISPLAY_AUTO_OFF_MS) {
        backlight_set(false);
        xSemaphoreGive(s_disp_mutex);
        return;     /* Don't render when display is off */
    }

    if (!s_backlight_on) {
        xSemaphoreGive(s_disp_mutex);
        return;
    }

    /* Render the current screen */
    switch (s_current_screen) {
    case SCREEN_STATUS:
        render_status_screen(status);
        break;
    case SCREEN_ALERT:
        render_alert_screen(status);
        break;
    case SCREEN_DEVICES:
        render_devices_screen(status);
        break;
    default:
        render_status_screen(status);
        break;
    }

    xSemaphoreGive(s_disp_mutex);
}

void display_next_screen(void)
{
    xSemaphoreTake(s_disp_mutex, portMAX_DELAY);
    s_current_screen = (screen_id_t)((s_current_screen + 1) % SCREEN_COUNT);
    s_last_activity_us = esp_timer_get_time();
    xSemaphoreGive(s_disp_mutex);

    display_wake();
    display_update(&s_last_status);

    ESP_LOGD(TAG, "Screen: %d", s_current_screen);
}

void display_prev_screen(void)
{
    xSemaphoreTake(s_disp_mutex, portMAX_DELAY);
    if (s_current_screen == 0) {
        s_current_screen = (screen_id_t)(SCREEN_COUNT - 1);
    } else {
        s_current_screen = (screen_id_t)(s_current_screen - 1);
    }
    s_last_activity_us = esp_timer_get_time();
    xSemaphoreGive(s_disp_mutex);

    display_wake();
    display_update(&s_last_status);

    ESP_LOGD(TAG, "Screen: %d", s_current_screen);
}

void display_wake(void)
{
    s_last_activity_us = esp_timer_get_time();

    if (!s_backlight_on) {
        backlight_set(true);
        ESP_LOGD(TAG, "Display woke");
    }
}

void display_off(void)
{
    backlight_set(false);
    ESP_LOGD(TAG, "Display forced off");
}
