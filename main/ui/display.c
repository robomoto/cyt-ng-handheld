/**
 * AMOLED display driver — RM67162 240x536 on LilyGo T-Display-S3 AMOLED.
 *
 * Uses QSPI (quad-SPI) to communicate with the RM67162 driver IC.
 * The display is connected to fixed internal pins on the board:
 *   CS=6, SCK=47, D0=18, D1=7, D2=48, D3=5, RST=17, TE=9
 *
 * Text-only rendering using a minimal 8x16 bitmap font — same as the
 * ST7789 driver it replaces, but adapted for the wider 30-column,
 * 33-line character grid.
 */

#include "ui/display.h"
#include "cyt_config.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "display";

/* ── T-Display-S3 AMOLED QSPI pin definitions ─────────────────── */

#define AMOLED_PIN_CS       6
#define AMOLED_PIN_SCK      47
#define AMOLED_PIN_D0       18      /* SPI MOSI / quad data 0 */
#define AMOLED_PIN_D1       7       /* quad data 1 */
#define AMOLED_PIN_D2       48      /* quad data 2 */
#define AMOLED_PIN_D3       5       /* quad data 3 */
#define AMOLED_PIN_RST      17
#define AMOLED_PIN_TE       9       /* Tearing effect (active-low IRQ) */

/* ── RM67162 command definitions ───────────────────────────────── */

#define RM67162_CMD_NOP         0x00
#define RM67162_CMD_SWRESET     0x01
#define RM67162_CMD_SLPIN       0x10
#define RM67162_CMD_SLPOUT      0x11
#define RM67162_CMD_INVOFF      0x20
#define RM67162_CMD_INVON       0x21
#define RM67162_CMD_DISPOFF     0x28
#define RM67162_CMD_DISPON      0x29
#define RM67162_CMD_CASET       0x2A    /* Column address set */
#define RM67162_CMD_RASET       0x2B    /* Row address set */
#define RM67162_CMD_RAMWR       0x2C    /* Memory write */
#define RM67162_CMD_MADCTL      0x36    /* Memory access control */
#define RM67162_CMD_COLMOD      0x3A    /* Pixel format */
#define RM67162_CMD_BRIGHTNESS  0x51    /* Write display brightness */
#define RM67162_CMD_WRCACE      0x55    /* Write CABC */
#define RM67162_CMD_TEOFF       0x34
#define RM67162_CMD_TEON        0x35

/*
 * RM67162 QSPI command wire format:
 *
 *   Byte 0: command (sent on single SPI line)
 *   Byte 1: 0x00 (dummy / address high)
 *   Byte 2: 0x00 (address mid)
 *   Byte 3: 0x00 (address low)
 *   Then data bytes in quad mode.
 *
 * For pixel writes (RAMWR), the 24-bit address field carries the
 * column/row setup implicitly (pre-set via CASET/RASET).
 *
 * We use ESP-IDF spi_master with the SPI_TRANS_MODE_QIO flag for
 * quad-mode data phases.
 */

/* ── Display constants ──────────────────────────────────────────── */

#define DISPLAY_W           CYT_DISPLAY_WIDTH   /* 240 */
#define DISPLAY_H           CYT_DISPLAY_HEIGHT  /* 536 */
#define FONT_W              8
#define FONT_H              16
#define CHARS_PER_LINE      (DISPLAY_W / FONT_W)    /* 30 chars */
#define LINES_PER_SCREEN    (DISPLAY_H / FONT_H)    /* 33 lines */

/* QSPI clock speed — RM67162 supports up to 80 MHz */
#define QSPI_CLK_HZ        (40 * 1000 * 1000)

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

static spi_device_handle_t     s_spi_dev;
static screen_id_t             s_current_screen = SCREEN_STATUS;
static display_status_t        s_last_status;
static SemaphoreHandle_t       s_disp_mutex;
static int64_t                 s_last_activity_us;
static bool                    s_display_on;
static bool                    s_initialized;

/* Line pixel buffer — one row of pixels in RGB565 for drawing */
static uint16_t s_line_buf[DISPLAY_W];

/* ── QSPI low-level transport ──────────────────────────────────── */

/**
 * Send a command with optional parameter bytes to the RM67162.
 *
 * The RM67162 QSPI protocol sends the command byte on a single SPI
 * line, followed by a 24-bit address (set to 0x000000 for register
 * commands), then parameter data in quad mode.
 *
 * For commands with no data, we just send the 4-byte header.
 */
static esp_err_t rm67162_send_cmd(uint8_t cmd, const uint8_t *data, size_t len)
{
    spi_transaction_ext_t ext = {
        .base = {
            .flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR,
            .cmd   = cmd,
            .addr  = 0x000000,
            .length   = len * 8,
            .tx_buffer = data,
        },
        .command_bits = 8,
        .address_bits = 24,
    };

    /*
     * Command and address travel on 1 line; data phase uses 4 lines.
     * ESP-IDF SPI_TRANS_MULTILINE_CMD/ADDR flags tell the driver to
     * keep command/address on 1 line even when the device is
     * configured for quad mode.
     */
    if (len == 0) {
        ext.base.tx_buffer = NULL;
    }

    return spi_device_polling_transmit(s_spi_dev,
                                       (spi_transaction_t *)&ext);
}

/**
 * Write a rectangular block of RGB565 pixel data to the display.
 * Assumes CASET/RASET have already been configured.
 *
 * For large transfers we send the RAMWR command header first, then
 * the pixel data in quad mode using DMA-capable buffer.
 */
static esp_err_t rm67162_write_pixels(const uint16_t *pixels, size_t pixel_count)
{
    /*
     * The RM67162 expects big-endian RGB565.  ESP32-S3 is little-endian,
     * so we byte-swap each pixel before sending.  For line-at-a-time
     * rendering the overhead is negligible.
     */
    spi_transaction_ext_t ext = {
        .base = {
            .flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR,
            .cmd   = RM67162_CMD_RAMWR,
            .addr  = 0x000000,
            .length   = pixel_count * 16,   /* bits */
            .tx_buffer = pixels,
        },
        .command_bits = 8,
        .address_bits = 24,
    };

    return spi_device_polling_transmit(s_spi_dev,
                                       (spi_transaction_t *)&ext);
}

/**
 * Set the column (x) address window.
 */
static void rm67162_set_column(uint16_t start, uint16_t end)
{
    uint8_t data[4] = {
        (uint8_t)(start >> 8), (uint8_t)(start & 0xFF),
        (uint8_t)(end >> 8),   (uint8_t)(end & 0xFF),
    };
    rm67162_send_cmd(RM67162_CMD_CASET, data, sizeof(data));
}

/**
 * Set the row (y) address window.
 */
static void rm67162_set_row(uint16_t start, uint16_t end)
{
    uint8_t data[4] = {
        (uint8_t)(start >> 8), (uint8_t)(start & 0xFF),
        (uint8_t)(end >> 8),   (uint8_t)(end & 0xFF),
    };
    rm67162_send_cmd(RM67162_CMD_RASET, data, sizeof(data));
}

/**
 * Set the address window for a rectangular region, then send RAMWR
 * to begin writing pixels.
 */
static void rm67162_set_window(uint16_t x0, uint16_t y0,
                                uint16_t x1, uint16_t y1)
{
    rm67162_set_column(x0, x1 - 1);
    rm67162_set_row(y0, y1 - 1);
}

/* ── AMOLED power control (replaces backlight PWM) ─────────────── */

/**
 * Turn the display panel on — exit sleep, enable pixel output.
 * AMOLED has no backlight; the panel itself emits light.
 */
static void amoled_panel_on(void)
{
    if (!s_initialized) return;

    rm67162_send_cmd(RM67162_CMD_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    rm67162_send_cmd(RM67162_CMD_DISPON, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Set brightness to ~80% */
    uint8_t brightness = 0xCC;
    rm67162_send_cmd(RM67162_CMD_BRIGHTNESS, &brightness, 1);

    s_display_on = true;
}

/**
 * Turn the display panel off — pixels off, then enter sleep for
 * minimal power draw (~10 uA).
 */
static void amoled_panel_off(void)
{
    if (!s_initialized) return;

    rm67162_send_cmd(RM67162_CMD_DISPOFF, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    rm67162_send_cmd(RM67162_CMD_SLPIN, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    s_display_on = false;
}

/* ── Drawing primitives ─────────────────────────────────────────── */

/**
 * Byte-swap a line buffer from native LE to the BE order RM67162 expects.
 */
static void swap_line_buf(uint16_t *buf, int count)
{
    for (int i = 0; i < count; i++) {
        buf[i] = __builtin_bswap16(buf[i]);
    }
}

/**
 * Fill a rectangular region with a solid color.
 */
static void draw_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w > DISPLAY_W) w = DISPLAY_W;
    if (x + w > DISPLAY_W) w = DISPLAY_W - x;
    if (y + h > DISPLAY_H) h = DISPLAY_H - y;
    if (w <= 0 || h <= 0) return;

    uint16_t be_color = __builtin_bswap16(color);
    for (int i = 0; i < w; i++) {
        s_line_buf[i] = be_color;
    }

    for (int row = y; row < y + h; row++) {
        rm67162_set_window(x, row, x + w, row + 1);
        rm67162_write_pixels(s_line_buf, w);
    }
}

/**
 * Draw a single character at pixel position (x, y) with fg/bg colors.
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
            /* Byte-swap and send this 8-pixel row */
            swap_line_buf(s_line_buf, FONT_W);
            rm67162_set_window(x, py, x + FONT_W, py + 1);
            rm67162_write_pixels(s_line_buf, FONT_W);
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
    draw_text_line(0, "      CYT-NG HANDHELD", COLOR_BLACK, COLOR_CYAN);

    /* Blank separator */
    draw_text_line(1, "", COLOR_WHITE, COLOR_BLACK);

    /* Device counts */
    snprintf(buf, sizeof(buf), "DEVICES: %lu", (unsigned long)st->total_devices);
    draw_text_line(3, buf, COLOR_WHITE, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "WIFI: %lu  BLE: %lu",
             (unsigned long)st->wifi_count, (unsigned long)st->ble_count);
    draw_text_line(5, buf, COLOR_GREEN, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "TPMS: %lu  DRONE: %lu",
             (unsigned long)st->tpms_count, (unsigned long)st->drone_count);
    draw_text_line(6, buf, COLOR_GREEN, COLOR_BLACK);

    draw_text_line(7, "", COLOR_WHITE, COLOR_BLACK);
    draw_text_line(8, "", COLOR_WHITE, COLOR_BLACK);

    /* Alert count */
    uint16_t alert_color = st->suspicious_count > 0 ? COLOR_RED : COLOR_GREEN;
    snprintf(buf, sizeof(buf), "ALERTS: %lu", (unsigned long)st->suspicious_count);
    draw_text_line(9, buf, alert_color, COLOR_BLACK);

    draw_text_line(10, "", COLOR_WHITE, COLOR_BLACK);

    /* GPS status */
    snprintf(buf, sizeof(buf), "GPS: %s", st->gps_fix ? "FIX OK" : "NO FIX");
    draw_text_line(12, buf, st->gps_fix ? COLOR_GREEN : COLOR_YELLOW, COLOR_BLACK);

    /* Battery */
    snprintf(buf, sizeof(buf), "BATT: %u%%", st->battery_percent);
    uint16_t batt_color = st->battery_percent > 20 ? COLOR_GREEN : COLOR_RED;
    draw_text_line(14, buf, batt_color, COLOR_BLACK);

    /* SD status */
    snprintf(buf, sizeof(buf), "SD: %s",
             st->sd_ready ? (st->session_active ? "LOGGING" : "READY") : "NONE");
    uint16_t sd_color = st->sd_ready ? COLOR_GREEN : COLOR_RED;
    draw_text_line(16, buf, sd_color, COLOR_BLACK);

    /* Clear remaining lines */
    for (int i = 2; i < LINES_PER_SCREEN; i++) {
        /* Skip lines we already drew */
        if (i == 3 || i == 5 || i == 6 || i == 9 || i == 12 ||
            i == 14 || i == 16) continue;
        if (i == 0 || i == 1 || i == 7 || i == 8 || i == 10) continue;
        draw_text_line(i, "", COLOR_WHITE, COLOR_BLACK);
    }
}

static void render_alert_screen(const display_status_t *st)
{
    char buf[CHARS_PER_LINE + 1];

    /* Title bar */
    draw_text_line(0, "       ALERT DETAIL", COLOR_BLACK, COLOR_RED);

    draw_text_line(1, "", COLOR_WHITE, COLOR_BLACK);

    if (st->suspicious_count == 0) {
        draw_text_line(5, "       NO ALERTS", COLOR_GREEN, COLOR_BLACK);
        draw_text_line(7, "        ALL CLEAR", COLOR_GREEN, COLOR_BLACK);

        for (int i = 2; i < LINES_PER_SCREEN; i++) {
            if (i == 5 || i == 7) continue;
            draw_text_line(i, "", COLOR_WHITE, COLOR_BLACK);
        }
        return;
    }

    draw_text_line(3, "HIGHEST PERSISTENCE:", COLOR_YELLOW, COLOR_BLACK);

    /* Device ID — more room now (30 chars) */
    snprintf(buf, sizeof(buf), "ID: %.26s", st->highest_device_id);
    draw_text_line(5, buf, COLOR_WHITE, COLOR_BLACK);

    /* Persistence score */
    snprintf(buf, sizeof(buf), "SCORE: %.2f", st->highest_persistence);
    uint16_t score_color = st->highest_persistence >= 0.7f ? COLOR_RED : COLOR_ORANGE;
    draw_text_line(7, buf, score_color, COLOR_BLACK);

    draw_text_line(8, "", COLOR_WHITE, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "TOTAL ALERTS: %lu",
             (unsigned long)st->suspicious_count);
    draw_text_line(10, buf, COLOR_RED, COLOR_BLACK);

    draw_text_line(12, "CHECK DEVICE LIST", COLOR_YELLOW, COLOR_BLACK);
    draw_text_line(13, "FOR MORE DETAILS", COLOR_YELLOW, COLOR_BLACK);

    for (int i = 2; i < LINES_PER_SCREEN; i++) {
        if (i == 3 || i == 5 || i == 7 || i == 8 || i == 10 ||
            i == 12 || i == 13) continue;
        draw_text_line(i, "", COLOR_WHITE, COLOR_BLACK);
    }
}

static void render_devices_screen(const display_status_t *st)
{
    char buf[CHARS_PER_LINE + 1];

    /* Title bar */
    draw_text_line(0, "       DEVICE LIST", COLOR_BLACK, COLOR_GREEN);

    draw_text_line(1, "", COLOR_WHITE, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "%lu TOTAL TRACKED",
             (unsigned long)st->total_devices);
    draw_text_line(3, buf, COLOR_WHITE, COLOR_BLACK);

    draw_text_line(4, "", COLOR_WHITE, COLOR_BLACK);

    /*
     * In v1 we show summary counts only.  A full scrollable device list
     * requires the display task to query the device table directly,
     * which will be added in v2 with proper page-up/page-down support.
     */
    snprintf(buf, sizeof(buf), "WIFI:   %lu", (unsigned long)st->wifi_count);
    draw_text_line(6, buf, COLOR_CYAN, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "BLE:    %lu", (unsigned long)st->ble_count);
    draw_text_line(8, buf, COLOR_CYAN, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "TPMS:   %lu", (unsigned long)st->tpms_count);
    draw_text_line(10, buf, COLOR_CYAN, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "DRONE:  %lu", (unsigned long)st->drone_count);
    draw_text_line(12, buf, COLOR_CYAN, COLOR_BLACK);

    draw_text_line(13, "", COLOR_WHITE, COLOR_BLACK);

    snprintf(buf, sizeof(buf), "SUSPICIOUS: %lu",
             (unsigned long)st->suspicious_count);
    uint16_t color = st->suspicious_count > 0 ? COLOR_RED : COLOR_GREEN;
    draw_text_line(15, buf, color, COLOR_BLACK);

    /* Navigation hint — at bottom of taller display */
    draw_text_line(30, "UP/DOWN: SCROLL", COLOR_DARK_GRAY, COLOR_BLACK);
    draw_text_line(31, "MODE: NEXT SCREEN", COLOR_DARK_GRAY, COLOR_BLACK);

    /* Clear unused lines */
    for (int i = 2; i < LINES_PER_SCREEN; i++) {
        if (i == 3 || i == 6 || i == 8 || i == 10 || i == 12 ||
            i == 13 || i == 15 || i == 30 || i == 31) continue;
        draw_text_line(i, "", COLOR_WHITE, COLOR_BLACK);
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing RM67162 AMOLED display (240x536) via QSPI");

    s_disp_mutex = xSemaphoreCreateMutex();

    /* ── Hardware reset ──────────────────────────────────────────── */

    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << AMOLED_PIN_RST),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_cfg);

    gpio_set_level(AMOLED_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(AMOLED_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* ── QSPI bus configuration ──────────────────────────────────── */

    spi_bus_config_t bus_cfg = {
        .sclk_io_num     = AMOLED_PIN_SCK,
        .data0_io_num    = AMOLED_PIN_D0,
        .data1_io_num    = AMOLED_PIN_D1,
        .data2_io_num    = AMOLED_PIN_D2,
        .data3_io_num    = AMOLED_PIN_D3,
        .max_transfer_sz = DISPLAY_W * FONT_H * sizeof(uint16_t) + 64,
        .flags           = SPICOMMON_BUSFLAG_QUAD,
    };

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init QSPI bus: %s", esp_err_to_name(err));
        return;
    }

    spi_device_interface_config_t dev_cfg = {
        .command_bits    = 8,
        .address_bits    = 24,
        .mode            = 0,           /* CPOL=0, CPHA=0 */
        .clock_speed_hz  = QSPI_CLK_HZ,
        .spics_io_num    = AMOLED_PIN_CS,
        .queue_size      = 1,
        .flags           = SPI_DEVICE_HALFDUPLEX,
    };

    err = spi_bus_add_device(SPI2_HOST, &dev_cfg, &s_spi_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add QSPI device: %s", esp_err_to_name(err));
        return;
    }

    s_initialized = true;

    /* ── RM67162 initialization sequence ─────────────────────────── */

    /* Software reset */
    rm67162_send_cmd(RM67162_CMD_SWRESET, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(150));

    /* Exit sleep mode */
    rm67162_send_cmd(RM67162_CMD_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Pixel format: 16-bit RGB565 */
    uint8_t colmod = 0x55;
    rm67162_send_cmd(RM67162_CMD_COLMOD, &colmod, 1);

    /*
     * Memory access control — set scan direction for portrait mode.
     * Bit layout: MY | MX | MV | ML | BGR | MH | 0 | 0
     *
     * For the T-Display-S3 AMOLED in portrait (240 wide, 536 tall):
     *   0x00 = normal top-to-bottom, left-to-right, RGB order
     */
    uint8_t madctl = 0x00;
    rm67162_send_cmd(RM67162_CMD_MADCTL, &madctl, 1);

    /* Enable tearing-effect output on TE pin (vsync signal) */
    uint8_t te_mode = 0x00;     /* V-blank only */
    rm67162_send_cmd(RM67162_CMD_TEON, &te_mode, 1);

    /* Set brightness */
    uint8_t brightness = 0xCC;  /* ~80% */
    rm67162_send_cmd(RM67162_CMD_BRIGHTNESS, &brightness, 1);

    /* Content adaptive brightness: off (we control it manually) */
    uint8_t cabc = 0x00;
    rm67162_send_cmd(RM67162_CMD_WRCACE, &cabc, 1);

    /* Normal display mode on */
    rm67162_send_cmd(0x13, NULL, 0);  /* NORON */

    /* Display ON */
    rm67162_send_cmd(RM67162_CMD_DISPON, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    s_display_on = true;

    /* Clear to black */
    draw_clear(COLOR_BLACK);

    /* Show boot message — centered for 30-char width */
    draw_text_line(10, "     CYT-NG v1.0", COLOR_CYAN, COLOR_BLACK);
    draw_text_line(12, "     INITIALIZING...", COLOR_WHITE, COLOR_BLACK);

    s_last_activity_us = esp_timer_get_time();
    memset(&s_last_status, 0, sizeof(s_last_status));

    ESP_LOGI(TAG, "AMOLED display initialized (240x536, QSPI @ %d MHz)",
             QSPI_CLK_HZ / 1000000);
}

void display_update(const display_status_t *status)
{
    if (!s_initialized || !status) {
        return;
    }

    xSemaphoreTake(s_disp_mutex, portMAX_DELAY);

    /* Copy status for use by renderers */
    memcpy(&s_last_status, status, sizeof(s_last_status));

    /* Check auto-off timer */
    int64_t now = esp_timer_get_time();
    int64_t idle_ms = (now - s_last_activity_us) / 1000;

    if (s_display_on && idle_ms > CYT_DISPLAY_AUTO_OFF_MS) {
        amoled_panel_off();
        xSemaphoreGive(s_disp_mutex);
        return;     /* Don't render when display is off */
    }

    if (!s_display_on) {
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

    if (!s_display_on) {
        amoled_panel_on();
        ESP_LOGD(TAG, "AMOLED woke");
    }
}

void display_off(void)
{
    amoled_panel_off();
    ESP_LOGD(TAG, "AMOLED forced off (sleep mode)");
}
