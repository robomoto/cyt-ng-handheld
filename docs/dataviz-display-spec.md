# Data Visualization Spec: CYT-NG Handheld Display

**Author:** Data Visualization Specialist
**Date:** 2026-03-16
**Target:** LilyGo T-Display-S3, ST7789 170x320 TFT, 16-bit color

---

## Hardware Constraints Summary

- **Resolution:** 170x320 pixels, RGB565 (16-bit color, 65K colors)
- **Text grid:** 8x16 bitmap font = 21 characters x 20 lines
- **Rendering:** Direct pixel writes via `esp_lcd_panel_draw_bitmap`, line-buffer approach (170 pixels per scanline). No graphics library.
- **Refresh:** Every 2 seconds from analysis task on Core 1
- **CPU budget:** Must complete full screen redraw well within 2s. Current line-by-line approach is fast (each `draw_text_line` pushes 170x16 = 2,720 pixels). A full 20-line screen is ~54,400 pixels, trivial at 10 MHz pixel clock.
- **Interaction model:** 3 buttons (up, down, mode). Users glance for 1-2 seconds.
- **Auto-off:** Backlight dims after 30 seconds of inactivity.

---

## 1. Status Screen -- Visual Hierarchy

The status screen must answer one question in under 1 second: **"Am I being tracked right now?"**

### 1.1 Background Color as Ambient Threat Indicator

**Yes -- shift the entire screen background based on threat level.** This is the single most effective use of the 65K-color display. It is visible in peripheral vision, through pocket fabric (backlight glow), and requires zero cognitive processing.

Three states:

| State | Condition | Background | Rationale |
|-------|-----------|------------|-----------|
| Safe | `highest_persistence < 0.5` AND `suspicious_count == 0` | Black (`0x0000`) | Default. Conserves power on OLED-style displays, high contrast for text. |
| Notable | `highest_persistence >= 0.5` AND `< 0.7` | Dark amber (`0x2100`) | Warm tone visible in periphery. Not alarming -- just "something is here." |
| Elevated | `highest_persistence >= 0.7` OR `suspicious_count >= 3` | Dark red (`0x2000`) | Unmistakable. Even a 1-second glance registers "red screen." |

**Implementation:** Replace the hardcoded `COLOR_BLACK` background in `render_status_screen` with a computed `bg_color` variable. Pass it to every `draw_text_line` call. This is a 1-line computation and zero additional drawing cost -- the background pixels are already being written per-character.

```c
static uint16_t threat_bg_color(const display_status_t *st)
{
    if (st->highest_persistence >= 0.7f || st->suspicious_count >= 3)
        return 0x2000;  /* dark red */
    if (st->highest_persistence >= 0.5f)
        return 0x2100;  /* dark amber */
    return COLOR_BLACK;
}
```

### 1.2 Persistence Bar vs. Raw Number

**Use both.** The bar gives instant magnitude; the number gives precision.

Format on a single line (21 chars available):

```
[========--] 0.82
```

That is: 1 bracket + 10 bar chars + 1 bracket + 1 space + 4 chars (X.XX) = 18 chars. Fits with 3 chars to spare.

Bar characters:
- Filled: `=` (ASCII 0x3D) -- renders well in the bitmap font
- Empty: `-` (ASCII 0x2D) -- already has a glyph

The bar fill count is `round(persistence * 10)`. Color the entire bar line:

| Persistence | Bar + Number Color |
|------------|-------------------|
| < 0.5 | Green (`0x07E0`) |
| 0.5 -- 0.69 | Yellow (`0xFFE0`) |
| 0.7 -- 0.89 | Orange (`0xFD20`) |
| >= 0.9 | Red (`0xF800`) |

**Do not use Unicode block characters** (U+2588 etc.). The bitmap font only covers ASCII 0x20-0x7E. Stick with `=` and `-`.

### 1.3 Source Counts Layout

Four scanner types in 2 lines using fixed-width columns:

```
W:  47  B:  12
T:   3  D:   8
```

Each column is 10 chars wide (1 letter + 1 colon + 4 digits + padding). This fits 2 columns in 21 chars. The single-letter prefix is colored per source type (see Color Palette, Section 6).

**Do not spell out "WiFi" or "TPMS".** At 21 chars wide, abbreviation is mandatory. The single-letter codes are defined in the device list (Section 2) and used consistently everywhere.

### 1.4 Complete Status Screen Layout (20 lines)

```
Line  Content                    FG Color         BG Color
----  -------                    --------         --------
 0    " CYT-NG  12:34  G*B"     Black            Cyan (title bar)
 1    (blank)                    --               threat_bg
 2    "W:  47  B:  12"           per-source       threat_bg
 3    "T:   3  D:   8"           per-source       threat_bg
 4    (blank)                    --               threat_bg
 5    "[========--] 0.82"        persistence clr  threat_bg
 6    "AA:BB:CC:DD:EE:FF"        White            threat_bg
 7    (blank)                    --               threat_bg
 8    "ALERTS: 3"                Red/Green         threat_bg
 9    (blank)                    --               threat_bg
10    "SESSION: 2h14m"           Dark gray        threat_bg
11    "SD: LOGGING  847ev"       Green/Red        threat_bg
12-18 (blank)                    --               threat_bg
19    "< STATUS  1/3    >"       Dark gray        threat_bg
```

Header line 0 packs: label, time (HH:MM), GPS indicator (`G` = green when fix, dim when no fix), Bluetooth phone connection (`*` = connected, dim dot when disconnected), battery as single char (`B` colored green/yellow/red based on level).

**Rationale for header density:** These are status indicators the user checks less frequently. Packing them into the title bar frees the main body for the critical threat information.

Line 19 is a persistent navigation footer showing current screen position. The `<` and `>` hint that buttons switch screens.

---

## 2. Device List -- Information Density

### 2.1 What Fits in 2 Lines Per Device

With 21 chars per line and 20 total lines, minus 2 for header and 1 for footer = 17 usable lines = 8 device slots at 2 lines each, plus 1 spare line for a separator or page indicator.

**Line 1 (identification):** Source letter + truncated device ID + persistence score

```
W AA:BB:CC:DD:E 0.82
```

That is: 1 source char + 1 space + 14 ID chars + 1 space + 4 score chars = 21 chars exactly.

**Line 2 (detail):** RSSI + appearance count + time window indicator

```
  -67dB x14 [**--]
```

That is: 2 spaces (indent to show it belongs to line 1) + 5 RSSI chars + 1 space + 3 appearance chars + 1 space + 6 window chars = 18 chars.

The window indicator `[**--]` shows 4 time windows (5/10/15/20 min) as `*` (device seen in that window) or `-` (not seen). This directly maps to `device_record_t.window_flags` bits 0-3. This is the most compact representation of temporal persistence.

### 2.2 Truncation Strategy for Device IDs

Device IDs are 6 raw bytes in `device_record_t.device_id`. The display representation depends on source type:

| Source | Raw ID | Display Format | Example (14 chars) |
|--------|--------|----------------|---------------------|
| WiFi | MAC address | `XX:XX:XX:XX:X` (truncate last byte+colon) | `AA:BB:CC:DD:E` |
| BLE | Payload hash | `XX:XX:XX:XX:X` (same format) | `A1:B2:C3:D4:E` |
| TPMS | Sensor ID (4 bytes) | `0xXXXXXXXX` + padding | `0x1A2B3C4D   ` |
| Drone | Serial hash | `XX:XX:XX:XX:X` (same format) | `D1:E2:F3:A4:B` |

**Always truncate from the right.** The leftmost bytes of a MAC are the OUI (manufacturer), which is the most identifying portion. For BLE payload hashes, no part is more significant than another, but consistent left-truncation is predictable.

**Do not use prefixes like "findmy:" or "tpms:" in the device list.** The single-letter source type indicator at the start of line 1 already communicates this. Spending 7 chars on a prefix wastes a third of the line.

If the device has a probed SSID (WiFi only), show it on line 2 instead of the RSSI/count data when the user presses down to "expand" the selected device. This is a v2 feature.

### 2.3 Color Coding Strategy

**Color the source letter and the persistence score. Leave the device ID white.**

Rationale:
- The source letter is a single character -- coloring it by source type (W=green, B=blue, T=orange, D=red) makes it scannable without reading.
- The persistence score is the decision-critical number -- coloring it by severity (green/yellow/orange/red) draws the eye to high-threat devices.
- The device ID is reference information -- white on dark background is maximally readable. Coloring it would fight with the source and score colors.
- **Do not color the background per-row.** Alternating row backgrounds waste contrast budget and create visual noise on a 170px-wide display. The ambient background color (from threat level) already provides context.

### 2.4 Sort Order and Indicator

Default sort: persistence score descending (most suspicious first). This means the user sees the most important devices without scrolling.

Show sort order in the header line:

```
DEVICES  pg1/3  SCORE
```

Where `SCORE` indicates the current sort key. If future versions add sort-by-source or sort-by-recency, the label changes. The `pg1/3` shows scroll position.

### 2.5 Selected Row Indicator

The currently selected row (for the "expand detail" feature in v2) uses an inverted color scheme: black text on a white background for line 1 of the selected device. This is a standard selection indicator that requires no additional drawing primitives -- just swap fg and bg in the `draw_text_line` call.

---

## 3. Alert Severity Encoding

### 3.1 Multi-Channel Encoding (Redundant Signals)

Relying on color alone fails for colorblind users and in bright sunlight where colors wash out. Use **three redundant channels**:

| Channel | Safe | Notable | Elevated | Critical |
|---------|------|---------|----------|----------|
| **Background** | Black | Dark amber | Dark red | Dark red + flash |
| **Text prefix** | (none) | `+` | `!` | `!!` |
| **Left-edge bar** | None | 2px amber bar | 2px red bar | 4px red bar |

**Text prefix symbols:** Use ASCII characters that are already in the bitmap font. Avoid Unicode symbols -- the font only covers 0x20-0x7E.

- ` ` (space) = safe, no prefix
- `+` = notable (persistence 0.5-0.69)
- `!` = elevated (persistence 0.7-0.89)
- `!!` = critical (persistence >= 0.9)

These prefixes appear at the start of the device ID line in the device list:

```
W+AA:BB:CC:DD:E 0.62
W!AA:BB:CC:DD:E 0.84
```

The prefix costs 1 character (or 2 for critical), which shortens the ID by the same amount. Acceptable tradeoff.

### 3.2 Left-Edge Color Bar

Draw a colored rectangle on the left margin of alert-worthy lines. This is a `draw_fill_rect` call -- already implemented in the codebase.

```c
/* Draw left-edge severity bar */
if (persistence >= 0.9f)
    draw_fill_rect(0, y, 4, FONT_H * 2, COLOR_RED);
else if (persistence >= 0.7f)
    draw_fill_rect(0, y, 2, FONT_H * 2, COLOR_RED);
else if (persistence >= 0.5f)
    draw_fill_rect(0, y, 2, FONT_H * 2, COLOR_ORANGE);
```

This produces a thin colored stripe on the left edge of each device entry. It is visible even when the user is not reading the text -- the eye catches the colored edge in peripheral vision.

**Cost:** One additional `draw_fill_rect` per device row. At 8 devices maximum, this is 8 extra calls per refresh -- negligible.

### 3.3 Flashing for Critical Alerts

When `highest_persistence >= 0.9`, toggle the title bar between normal colors and inverted (red on white) on alternate refresh cycles. Since refresh is every 2 seconds, this produces a 0.5 Hz flash -- slow enough to not be seizure-inducing, fast enough to catch attention.

Implementation: use a static `bool s_flash_toggle` that flips each refresh cycle. No timer needed.

```c
static bool s_flash_toggle = false;
/* In render_status_screen: */
s_flash_toggle = !s_flash_toggle;
if (st->highest_persistence >= 0.9f && s_flash_toggle) {
    draw_text_line(0, " !! ALERT !!  ", COLOR_RED, COLOR_WHITE);
} else {
    draw_text_line(0, header_text, COLOR_BLACK, COLOR_CYAN);
}
```

---

## 4. Compact Data Representations

### 4.1 Source Type -- Single Character

| Source | Char | Color | Mnemonic |
|--------|------|-------|----------|
| WiFi | `W` | Green (`0x07E0`) | **W**iFi |
| BLE | `B` | Blue (`0x001F`) | **B**LE |
| TPMS | `T` | Orange (`0xFD20`) | **T**PMS |
| Drone | `D` | Red (`0xF800`) | **D**rone |

These characters are used everywhere: status screen source counts, device list rows, alert detail screen. Consistency matters more than cleverness.

### 4.2 Time Spans

Use the shortest unambiguous format. Context determines the format:

| Context | Format | Example | Rationale |
|---------|--------|---------|-----------|
| Session duration | `Xh Xm` | `2h14m` | 6 chars. Hours and minutes are both useful for sessions. |
| "Last seen" relative time | `Xm` or `Xs` | `3m` or `45s` | 2-3 chars. On the alert detail screen. |
| First/last seen absolute | `HH:MM` | `14:32` | 5 chars. 24-hour format avoids AM/PM ambiguity. |

**Do not use "ago" suffix.** It costs 4 characters and adds no information when the context is clearly a relative time field.

### 4.3 Location Count

Format: `Xlo` (number + "lo" for locations).

Examples: `1lo`, `3lo`, `12lo`

At 3-4 characters this fits in the device detail line. "lo" is sufficiently mnemonic and avoids confusion with other abbreviations.

### 4.4 GPS Fix Indicator

Single character in the title bar: `G`

| State | Rendering |
|-------|-----------|
| Fix acquired | `G` in green (`0x07E0`) |
| No fix | `G` in dark gray (`0x2104`) |

**Do not use an asterisk or separate "GPS: FIX OK" line.** The current implementation wastes an entire line (line 8) on GPS status. A single colored character in the header frees that line for threat data.

### 4.5 Battery Indicator

Single character in the title bar: a digit representing battery level bracket.

| Battery | Char | Color |
|---------|------|-------|
| 80-100% | `4` | Green |
| 60-79% | `3` | Green |
| 40-59% | `2` | Yellow |
| 20-39% | `1` | Orange |
| 0-19% | `0` | Red |

Alternatively, use a percentage on the detail line when the user navigates to a "system info" sub-view. For the status screen header, a single colored digit suffices. The user learns the encoding once.

**Simpler alternative:** Just show `XX%` (3 chars) in the header. This costs 2 extra characters but requires no learning:

```
CYT 12:34 G*  78%
```

This is 21 chars exactly (with spacing). Recommended over the digit encoding for user friendliness.

### 4.6 Phone Connection Indicator

Single character in the title bar: `*`

| State | Rendering |
|-------|-----------|
| Phone connected (BLE GATT) | `*` in cyan (`0x07FF`) |
| Disconnected | `.` in dark gray (`0x2104`) |

Position it adjacent to the GPS indicator so the two connectivity states are visually grouped.

---

## 5. Screen Transitions

### 5.1 Hard Cut (Recommended)

**Use instant hard cuts.** No wipe, no scroll, no fade.

Rationale:
- At 2-second refresh intervals, a 100ms animation is technically possible but provides negative value. The user presses a button and wants to see the next screen NOW. Any delay, even 100ms, feels like lag on a device with physical buttons.
- Animation requires either double-buffering (another 170x320x2 = 108KB buffer, eating into PSRAM) or careful partial redraws that add code complexity.
- The display is 170px wide. There is no spatial relationship between screens that animation would communicate. The screens are a flat list, not a spatial layout.
- Every millisecond spent on transition is a millisecond not spent on scan processing on Core 1. The display task runs on Core 0, but bus contention with SPI (SD card, CC1101) still exists.

### 5.2 Button Response

When a button is pressed, the screen should redraw immediately (not wait for the next 2-second tick). The current implementation already does this -- `display_next_screen` calls `display_update` directly. This gives sub-100ms response to button presses, which feels instant.

### 5.3 Screen Change Indicator

To give the user feedback that the screen changed (since there is no animation), flash the title bar for a single frame. On the first render after a screen change, draw the title bar in inverted colors (e.g., black on white), then on the next 2-second refresh, draw it normally. This is a 2-second "flash" that confirms the screen transition without any animation overhead.

Alternatively, simply rely on the different title bar colors per screen (cyan for status, red for alert, green for devices). The current implementation already does this. This is sufficient.

---

## 6. Color Palette

All values are RGB565 (16-bit, 5-6-5 bit split). Format: `0xRRRR` as 16-bit hex.

### 6.1 RGB565 Encoding Reference

```
RGB565 = ((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3)
```

Where R, G, B are 8-bit values (0-255).

### 6.2 Background Colors

| Name | RGB565 | RGB888 Approx | Usage |
|------|--------|---------------|-------|
| Safe (black) | `0x0000` | `#000000` | Default background, no threat |
| Notable (dark amber) | `0x2100` | `#201000` | Persistence 0.5-0.69 |
| Elevated (dark red) | `0x2000` | `#200000` | Persistence >= 0.7 or suspicious_count >= 3 |
| Title: Status | `0x07FF` | Cyan | Status screen title bar bg |
| Title: Alert | `0xF800` | Red | Alert screen title bar bg |
| Title: Devices | `0x07E0` | Green | Device list title bar bg |

### 6.3 Text Colors

| Name | RGB565 | RGB888 Approx | Usage |
|------|--------|---------------|-------|
| Primary (white) | `0xFFFF` | `#FFFFFF` | Device IDs, labels, default text |
| Secondary (gray) | `0x2104` | `#202020` | Dim: nav hints, inactive indicators, footer |
| Tertiary (light gray) | `0x7BEF` | `#7B7B7B` | Session info, SD status, secondary data |

### 6.4 Source Type Colors

| Source | RGB565 | RGB888 Approx | Contrast on Black | Notes |
|--------|--------|---------------|-------------------|-------|
| WiFi (green) | `0x07E0` | `#00FF00` | Excellent | Standard "WiFi = green" convention |
| BLE (blue) | `0x001F` | `#0000FF` | Poor -- **use `0x54BF`** | Pure blue is too dark on black. Use light blue `#52A0FF` instead. |
| TPMS (orange) | `0xFD20` | `#FFA500` | Excellent | Warm, distinct from WiFi green |
| Drone (red) | `0xF800` | `#FF0000` | Good | Standard "danger" association fits drones (highest threat profile) |

**BLE blue correction:** The current `COLOR_GREEN = 0x07E0` and hypothetical pure blue `0x001F` have very different luminance. On a small TFT viewed at angle, pure blue nearly disappears. Use `0x54BF` (approximately `#52A0FF`, a sky blue) for readable BLE indicators.

Updated BLE blue: **`0x54BF`** (RGB565) = R:10, G:37, B:31 in 5-6-5 = approximately `#52A5F8`.

### 6.5 Alert / Persistence Severity Colors

| Level | Persistence Range | RGB565 | RGB888 Approx | Usage |
|-------|------------------|--------|---------------|-------|
| Low (green) | < 0.5 | `0x07E0` | `#00FF00` | Score text, bar fill |
| Notable (yellow) | 0.5 -- 0.69 | `0xFFE0` | `#FFFF00` | Score text, bar fill |
| Elevated (orange) | 0.7 -- 0.89 | `0xFD20` | `#FFA500` | Score text, bar fill |
| Critical (red) | >= 0.9 | `0xF800` | `#FF0000` | Score text, bar fill, flashing header |

### 6.6 System Indicator Colors

| Indicator | State | RGB565 | Notes |
|-----------|-------|--------|-------|
| GPS fix | Yes | `0x07E0` (green) | |
| GPS fix | No | `0x2104` (dark gray) | Dim, not alarming |
| Battery > 40% | Normal | `0x07E0` (green) | |
| Battery 20-40% | Low | `0xFFE0` (yellow) | |
| Battery < 20% | Critical | `0xF800` (red) | |
| SD logging | Active | `0x07E0` (green) | |
| SD card | Missing | `0xF800` (red) | |
| Phone BLE | Connected | `0x07FF` (cyan) | |
| Phone BLE | Disconnected | `0x2104` (dark gray) | |

### 6.7 Color Constant Definitions for Code

```c
/* ---- Backgrounds ---- */
#define COLOR_BG_SAFE        0x0000   /* Black */
#define COLOR_BG_NOTABLE     0x2100   /* Dark amber */
#define COLOR_BG_ELEVATED    0x2000   /* Dark red */

/* ---- Text ---- */
#define COLOR_TEXT_PRIMARY    0xFFFF   /* White */
#define COLOR_TEXT_SECONDARY  0x2104   /* Dark gray */
#define COLOR_TEXT_TERTIARY   0x7BEF   /* Light gray */

/* ---- Source types ---- */
#define COLOR_SRC_WIFI       0x07E0   /* Green */
#define COLOR_SRC_BLE        0x54BF   /* Sky blue (not pure blue) */
#define COLOR_SRC_TPMS       0xFD20   /* Orange */
#define COLOR_SRC_DRONE      0xF800   /* Red */

/* ---- Persistence severity ---- */
#define COLOR_SEV_LOW        0x07E0   /* Green */
#define COLOR_SEV_NOTABLE    0xFFE0   /* Yellow */
#define COLOR_SEV_ELEVATED   0xFD20   /* Orange */
#define COLOR_SEV_CRITICAL   0xF800   /* Red */

/* ---- Title bars ---- */
#define COLOR_TITLE_STATUS   0x07FF   /* Cyan */
#define COLOR_TITLE_ALERT    0xF800   /* Red */
#define COLOR_TITLE_DEVICES  0x07E0   /* Green */

/* ---- System indicators ---- */
#define COLOR_CONNECTED      0x07FF   /* Cyan */
#define COLOR_INACTIVE       0x2104   /* Dark gray */
```

---

## 7. Implementation Priorities

### Phase 1 (Current v1 Improvements)

These changes modify the existing `display.c` with minimal structural change:

1. **Add `threat_bg_color()` function** and pass computed background to all `draw_text_line` calls in `render_status_screen`. Estimated: 15 lines of code.

2. **Add persistence bar** on line 5 of the status screen. Replace the plain "ALERTS: N" line with the `[========--] 0.82` format. Estimated: 10 lines.

3. **Compact the source counts** from 3 lines (DEVICES + WIFI/BLE + TPMS/DRONE) to 2 lines (`W:XX B:XX` / `T:XX D:XX`). Estimated: 5 lines changed.

4. **Pack the header line** with time, GPS, battery. Replace the dedicated GPS/battery/SD lines. Estimated: 15 lines.

5. **Replace BLE blue** `0x001F` references with `0x54BF`. Estimated: 1 line (add the constant).

### Phase 2 (v2 -- Scrollable Device List)

Requires the display task to query `device_table` directly:

1. **Add device list rendering** with 2-line-per-device format.
2. **Add left-edge severity bars** via `draw_fill_rect`.
3. **Add text severity prefixes** (`+`, `!`, `!!`).
4. **Add page indicator** and scroll state tracking.
5. **Add selected-row inversion** for detail expansion.

### Phase 3 (v2+ -- Polish)

1. **Title bar flash** for critical alerts (>= 0.9 persistence).
2. **Screen change confirmation** flash.
3. **SSID display** on expanded device detail (WiFi devices only).

---

## 8. What NOT to Build

- **No charts or graphs.** At 170px wide, a line chart has ~170 data points at 1px resolution. With an 8px font for axis labels, the actual plot area shrinks to ~130px. This conveys less information than a single number with color coding.

- **No sparklines.** A 10-character text sparkline using block characters (`_.-~^`) looks clever but is unreadable at arm's length on a 1.9" screen. The 4-character window indicator `[**--]` conveys the same temporal information more reliably.

- **No scrolling text / marquee.** Wastes CPU cycles, is unreadable during a 1-2 second glance, and creates visual noise.

- **No icons or custom bitmaps** (v1). The current font renderer handles ASCII 0x20-0x7E. Adding custom icon bitmaps is feasible but adds code complexity for minimal gain when single colored letters (`W`, `B`, `T`, `D`) already work. Reconsider for v3 if user testing shows the letters are confusing.

- **No anti-aliased text.** The 8x16 bitmap font is pixel-perfect at native resolution. Anti-aliasing on a 170px-wide display at 16-bit color would require sub-pixel rendering and look worse, not better.

- **No transition animations.** See Section 5.

---

## 9. Accessibility Notes

- **Color is never the sole channel.** Every color-coded element has a redundant text-based indicator: persistence has a numeric score and a bar, source type has a letter, severity has a text prefix, GPS has a letter that dims.

- **Contrast ratios.** White (`0xFFFF`) on black (`0x0000`) is maximum contrast. White on dark amber (`0x2100`) and white on dark red (`0x2000`) maintain high contrast because the background tints are deliberately very dark. If the background were brighter (e.g., `0x4000` for red), white text would lose contrast -- the dark values in this spec are chosen to keep text readable.

- **Colorblind considerations.** The green/red distinction (used for WiFi vs. Drone, and for low vs. critical severity) is problematic for protanopia/deuteranopia (~8% of males). Mitigations already in the spec:
  - Source types use letter codes (`W`, `B`, `T`, `D`) in addition to color
  - Severity uses text prefixes (`+`, `!`, `!!`) and the persistence number
  - The left-edge bar width (2px vs. 4px) provides a non-color severity cue
  - Background shift from black to amber to red is primarily a luminance change, not just a hue change, which is perceivable by most colorblind users

- **Viewing angle.** The ST7789 TFT has an IPS panel with ~170-degree viewing angles. Color accuracy degrades at extreme angles but remains usable. The high-contrast design (bright text on near-black backgrounds) tolerates angle-induced color shift better than a design relying on subtle color distinctions.
