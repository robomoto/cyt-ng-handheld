# CYT-NG Handheld: UI Screen Specification

Target: LilyGo T-Display-S3, ST7789 TFT, 170x320 pixels (portrait).
Font: 8x16 bitmap. Grid: 21 characters wide, 20 lines tall.
Buttons: UP (GPIO 2), DOWN (GPIO 3), MODE (GPIO 14).
Refresh: every 2 seconds from analysis task. Auto-dim after 30 seconds.

---

## Color Palette (RGB565)

These constants are already defined in `display.c` and should be extracted to a shared header.

| Name       | RGB565   | Use |
|------------|----------|-----|
| BLACK      | `0x0000` | Screen background |
| WHITE      | `0xFFFF` | Primary text, static labels |
| GREEN      | `0x07E0` | Good status, WiFi source, clear state |
| RED        | `0xF800` | REVIEW tier, critical alerts, low battery |
| YELLOW     | `0xFFE0` | NOTABLE tier, GPS no-fix, guidance text |
| ORANGE     | `0xFD20` | ELEVATED tier, TPMS source |
| CYAN       | `0x07FF` | Title bars, header backgrounds |
| BLUE       | `0x001F` | BLE source (add to palette) |
| MAGENTA    | `0xF81F` | Drone source (add to palette) |
| GRAY       | `0x2104` | Navigation hints, disabled text |

### Source Type Colors

| Source | Letter | Color   |
|--------|--------|---------|
| WiFi   | W      | GREEN   |
| BLE    | B      | BLUE    |
| TPMS   | T      | ORANGE  |
| Drone  | D      | MAGENTA |

### Persistence Tier Colors

| Score      | Tier            | Color   |
|------------|-----------------|---------|
| 0.0 - 0.3 | (silent)        | GREEN   |
| 0.3 - 0.5 | INFORMATIONAL   | GREEN   |
| 0.5 - 0.7 | NOTABLE         | YELLOW  |
| 0.7 - 0.85| ELEVATED        | ORANGE  |
| 0.85 - 1.0| REVIEW          | RED     |

---

## Button Interaction Model

| Input                      | Action |
|----------------------------|--------|
| Short press UP             | Scroll up within current screen |
| Short press DOWN           | Scroll down within current screen |
| Short press MODE           | Cycle screen: 0 -> 1 -> 2 -> 0 |
| Long press MODE (2s)       | Toggle session recording on/off |
| Long press UP+DOWN (3s)    | Enter/exit disguise mode |
| Triple press MODE (<1s)    | Trigger data wipe (if stealth configured) |
| Any button while dimmed    | Wake display (resets auto-off timer) |

The screen enum needs a fourth entry for disguise mode:

```c
typedef enum {
    SCREEN_STATUS  = 0,
    SCREEN_ALERT   = 1,
    SCREEN_DEVICES = 2,
    SCREEN_DISGUISE = 3,   /* Not in normal cycle */
    SCREEN_COUNT,
} screen_id_t;
```

MODE cycles through 0-1-2 only. SCREEN_DISGUISE is entered/exited exclusively via the UP+DOWN long-press combo and locks out normal screen cycling while active.

---

## Screen 0: Status (Home Screen)

The at-a-glance screen. A user should be able to read the situation in one second.

### Layout

```
Line  Content                 FG Color   BG Color   Dynamic?
----  ---------------------   ---------  ---------  --------
 0    " CYT-NG  ##:## xPR"   BLACK      CYAN       yes (time, indicators)
 1    "---------------------"  GRAY       BLACK      static
 2    "W:####  B:####       "  GREEN      BLACK      yes (counts)
 3    "T:####  D:####       "  GREEN      BLACK      yes (counts)
 4    "---------------------"  GRAY       BLACK      static
 5    "ALERTS: ##           "  *tier      BLACK      yes (count + color)
 6    " top alert text here "  *tier      BLACK      yes (conditional)
 7    "---------------------"  GRAY       BLACK      static
 8    "GPS:### BAT:###%     "  *status    BLACK      yes (both)
 9    "REC:#### PHONE:##    "  *status    BLACK      yes (both)
10-18 (empty)                  —          BLACK      —
19    "MODE:NEXT  UP/DN:SCR "  GRAY       BLACK      static
```

### ASCII Art Mockup

```
+---------------------+
| CYT-NG  14:23 .PR   |  <- line 0: header bar
|---------------------|
|W:0142  B:0008       |  <- line 2: WiFi + BLE counts
|T:0023  D:0000       |  <- line 3: TPMS + Drone counts
|---------------------|
|ALERTS: 2            |  <- line 5: alert count (RED)
| findmy:a1b2..REVIEW |  <- line 6: top device + tier
|---------------------|
|GPS:FIX BAT: 78%     |  <- line 8: GPS + battery
|REC:01:23 PHONE:OK   |  <- line 9: session + companion
|                     |
|                     |
|                     |
|                     |
|                     |
|                     |
|                     |
|                     |
|                     |
|MODE:NEXT  UP/DN:SCR |  <- line 19: nav hint
+---------------------+
```

### Element Details

**Line 0 — Header bar** (BLACK text on CYAN background):
- Format: ` CYT-NG  HH:MM xPR`
- `HH:MM` = current time from GPS or system clock. Dynamic.
- Right-side indicators (single characters, right-justified):
  - `.` or `*` = alert indicator. `.` = no alerts (CYAN, blends in). `*` = alerts present (RED on CYAN).
  - `P` = phone companion connected (WHITE). Space if disconnected.
  - `R` = session recording active (WHITE). Space if inactive.
- The header bar is persistent across screens 0-2 (same format, same position).

**Lines 2-3 — Device counts:**
- `W:` = WiFi count, `B:` = BLE count, `T:` = TPMS count, `D:` = Drone count.
- All GREEN text. Zero counts show as `0000`.
- Four digits per count (zero-padded) gives consistent layout.
- Dynamic: updated every 2-second refresh.

**Line 5 — Alert count:**
- `ALERTS: 0` in GREEN when no alerts.
- `ALERTS: N` in the color of the highest-tier alert present.
- Dynamic.

**Line 6 — Top alert summary** (conditional):
- Only drawn when alerts > 0. Blank when no alerts.
- Format: ` <device_id_truncated>..<TIER>`
- Device ID truncated to 13 chars, then `..`, then tier name.
- Color matches the tier color of that device.
- Example: ` findmy:a1b2..REVIEW` (RED)
- Example: ` AA:BB:CC:DD..NOTABLE` (YELLOW)
- Dynamic.

**Line 8 — GPS + Battery:**
- GPS portion: `GPS:FIX` (GREEN) or `GPS:---` (YELLOW).
- Battery portion: `BAT:###%`
  - 100-21%: GREEN
  - 20-6%: YELLOW (add YELLOW threshold)
  - 5-0%: RED
- Both dynamic.

**Line 9 — Recording + Phone:**
- `REC:` followed by session duration `HH:MM` (GREEN) or `OFF` (GRAY).
- `PHONE:` followed by `OK` (GREEN) or `--` (GRAY).
- Both dynamic.

**Line 19 — Navigation hint:**
- GRAY text. Static. Can be hidden after 60 seconds of use to reclaim the line.

### Data Source Mapping

| Field | Source in `display_status_t` | Notes |
|-------|------------------------------|-------|
| WiFi count | `wifi_count` | Exists |
| BLE count | `ble_count` | Exists |
| TPMS count | `tpms_count` | Exists |
| Drone count | `drone_count` | Exists |
| Alert count | `suspicious_count` | Exists |
| Top device ID | `highest_device_id` | Exists |
| Top persistence | `highest_persistence` | Exists, map to tier name |
| GPS fix | `gps_fix` | Exists |
| Battery | `battery_percent` | Exists |
| SD/recording | `sd_ready`, `session_active` | Exists |
| Time | — | **Add**: `uint32_t epoch` or `char time_str[6]` |
| Phone connected | — | **Add**: `bool companion_connected` |
| Session duration | — | **Add**: `uint32_t session_seconds` |

---

## Screen 1: Alert Detail

Shows detail for the highest-persistence suspicious device. When no alerts exist, shows an all-clear message.

### Layout — No Alerts

```
+---------------------+
| CYT-NG  14:23 .PR   |  <- line 0: shared header
|---------------------|
|                     |
|                     |
|                     |
|   ALL CLEAR         |  <- line 5: GREEN
|                     |
|   No devices need   |  <- line 7: WHITE
|   review.           |  <- line 8: WHITE
|                     |
|                     |
|                     |
|                     |
|                     |
|                     |
|                     |
|                     |
|                     |
|                     |
|MODE:NEXT  UP/DN:SCR |
+---------------------+
```

**Line 5**: `   ALL CLEAR` — GREEN text, BLACK background.
**Lines 7-8**: `   No devices need` / `   review.` — WHITE text. Calm, factual. Static when in this state.

### Layout — Alert Present

```
Line  Content                 FG Color   BG Color   Dynamic?
----  ---------------------   ---------  ---------  --------
 0    " CYT-NG  HH:MM xPR"   BLACK      CYAN       yes (shared header)
 1    "---------------------"  GRAY       BLACK      static
 2    " HIGHEST PERSISTENCE "  BLACK      *tier      yes (tier color bg)
 3    "ID:findmy:a1b2c3d4  "  WHITE      BLACK      yes
 4    "TIER: REVIEW         "  *tier      BLACK      yes
 5    "TYPE: BLE            "  BLUE       BLACK      yes (source color)
 6    "---------------------"  GRAY       BLACK      static
 7    "FIRST: 13:42  03/16  "  WHITE      BLACK      yes
 8    "LAST:  14:21  03/16  "  WHITE      BLACK      yes
 9    "LOCS: 4              "  WHITE      BLACK      yes
10    "---------------------"  GRAY       BLACK      static
11    (guidance line 1)        YELLOW     BLACK      yes (tier-dependent)
12    (guidance line 2)        YELLOW     BLACK      yes (tier-dependent)
13-18 (empty)                  —          BLACK      —
19    "MODE:NEXT  UP/DN:SCR "  GRAY       BLACK      static
```

### ASCII Art Mockup — Alert Present

```
+---------------------+
| CYT-NG  14:23 *PR   |  <- header, * = alert indicator RED
|---------------------|
| HIGHEST PERSISTENCE  |  <- line 2: BLACK on RED bg
|ID:findmy:a1b2c3d4   |  <- line 3: device ID
|TIER: REVIEW          |  <- line 4: RED text
|TYPE: BLE             |  <- line 5: BLUE text
|---------------------|
|FIRST: 13:42  03/16  |  <- line 7: first seen
|LAST:  14:21  03/16  |  <- line 8: last seen
|LOCS: 4              |  <- line 9: location count
|---------------------|
|Pattern is unusual.   |  <- line 11: guidance
|Review details.       |  <- line 12: guidance
|                     |
|                     |
|                     |
|                     |
|                     |
|                     |
|MODE:NEXT  UP/DN:SCR |
+---------------------+
```

### Element Details

**Line 2 — Section header:**
- ` HIGHEST PERSISTENCE` — BLACK text on tier-color background.
- Background color matches the tier of the displayed device:
  - INFORMATIONAL: GREEN background
  - NOTABLE: YELLOW background
  - ELEVATED: ORANGE background
  - REVIEW: RED background

**Line 3 — Device ID:**
- `ID:` prefix in WHITE, followed by up to 18 chars of device ID.
- Truncated if longer. No abbreviation suffix needed — the full 18 chars fit.
- Dynamic.

**Line 4 — Persistence tier:**
- `TIER: INFORMATIONAL` / `TIER: NOTABLE` / `TIER: ELEVATED` / `TIER: REVIEW`
- Text color matches tier color. Dynamic.
- Never show raw numeric score to the user (per social-psych guidance).

**Line 5 — Source type:**
- `TYPE: WIFI` / `TYPE: BLE` / `TYPE: TPMS` / `TYPE: DRONE`
- Text color matches source-type color. Dynamic.

**Lines 7-9 — Temporal/spatial data:**
- `FIRST:` and `LAST:` show HH:MM and MM/DD of first/last sighting. WHITE text.
- `LOCS:` shows the number of distinct GPS locations where the device was seen. WHITE text.
- All dynamic.

**Lines 11-12 — Guidance text:**
- Tier-dependent, calm, factual language. YELLOW text. No exclamation points.
- Language per tier (following social-psych review guidance):

| Tier | Line 11 | Line 12 |
|------|---------|---------|
| INFORMATIONAL | `Device observed` | `at multiple times.` |
| NOTABLE | `Observed at multiple` | `locations. May be` + line 13: `coincidental.` |
| ELEVATED | `Pattern across` | `locations. Consider` + line 13: `your safety plan.` |
| REVIEW | `Pattern is unusual.` | `Review details.` |

For NOTABLE and ELEVATED, use three guidance lines (11-13) to fit the message without abbreviating.

### Data Source Extensions

The `display_status_t` struct needs additional fields for this screen:

```c
/* Add to display_status_t */
uint8_t  highest_source_type;    /* source_type_t of top device */
uint32_t highest_first_seen;     /* epoch seconds */
uint32_t highest_last_seen;      /* epoch seconds */
uint8_t  highest_location_count; /* distinct GPS locations */
```

### Tier Mapping Function

```c
static const char *persistence_tier_name(float score)
{
    if (score >= 0.85f) return "REVIEW";
    if (score >= 0.7f)  return "ELEVATED";
    if (score >= 0.5f)  return "NOTABLE";
    if (score >= 0.3f)  return "INFORMATIONAL";
    return "";  /* silent — should not reach alert screen */
}
```

---

## Screen 2: Device List

Scrollable list of tracked devices, sorted by persistence (suspicious first). Two lines per device. UP/DOWN scrolls the list.

### Layout

```
Line  Content                 FG Color   BG Color   Dynamic?
----  ---------------------   ---------  ---------  --------
 0    " CYT-NG  HH:MM xPR"   BLACK      CYAN       yes (shared header)
 1    " DEVICES (####) P/PP"  WHITE      BLACK      yes (count + page)
 2    "---------------------"  GRAY       BLACK      static
 3    "findmy:a1b2c3d4     "  WHITE      BLACK      yes (device 1 ID)
 4    " B REVIEW     4 locs"  *tier+src  BLACK      yes (device 1 detail)
 5    "AA:BB:CC:DD:EE:FF   "  WHITE      BLACK      yes (device 2 ID)
 6    " W ELEVATED   3 locs"  *tier+src  BLACK      yes (device 2 detail)
 7    "tpms:0x1A2B3C4D     "  WHITE      BLACK      yes (device 3 ID)
 8    " T NOTABLE    2 locs"  *tier+src  BLACK      yes (device 3 detail)
 9    "AA:CC:DD:11:22:33   "  WHITE      BLACK      yes (device 4 ID)
10    " W INFORMATIONAL    "  *tier+src  BLACK      yes (device 4 detail)
11    "ble:e7f8a9b0c1d2    "  WHITE      BLACK      yes (device 5 ID)
12    " B INFORMATIONAL    "  *tier+src  BLACK      yes (device 5 detail)
13    "drone:DJI12345678   "  WHITE      BLACK      yes (device 6 ID)
14    " D NOTABLE    2 locs"  *tier+src  BLACK      yes (device 6 detail)
15    "tpms:0x5E6F7A8B     "  WHITE      BLACK      yes (device 7 ID)
16    " T INFORMATIONAL    "  *tier+src  BLACK      yes (device 7 detail)
17    "                     "  —          BLACK      (empty if < 8 devices)
18    "                     "  —          BLACK      —
19    " ^ UP     DOWN v    "  GRAY       BLACK      conditional (arrows)
```

### ASCII Art Mockup

```
+---------------------+
| CYT-NG  14:23 *PR   |  <- shared header
| DEVICES (0089) 1/03 |  <- total count + page
|---------------------|
|findmy:a1b2c3d4      |  <- device 1: ID line
| B REVIEW     4 locs |  <- device 1: source, tier, locs
|AA:BB:CC:DD:EE:FF    |  <- device 2
| W ELEVATED   3 locs |
|tpms:0x1A2B3C4D      |  <- device 3
| T NOTABLE    2 locs |
|AA:CC:DD:11:22:33    |  <- device 4
| W INFORMATIONAL     |
|ble:e7f8a9b0c1d2     |  <- device 5
| B INFORMATIONAL     |
|drone:DJI12345678    |  <- device 6
| D NOTABLE    2 locs |
|tpms:0x5E6F7A8B      |  <- device 7
| T INFORMATIONAL     |
|                     |
|                     |
| ^ UP     DOWN v     |  <- scroll indicators
+---------------------+
```

### Element Details

**Line 1 — Subheader:**
- ` DEVICES (####) P/PP` — total device count (zero-padded to 4) and current page / total pages.
- WHITE text on BLACK. Dynamic.

**Lines 3-16 — Device entries (7 devices per page):**
- Two lines per device:
  - **ID line** (odd lines 3,5,7,...): Device ID truncated to 21 chars. WHITE text.
  - **Detail line** (even lines 4,6,8,...): Format: ` X TIER     N locs`
    - `X` = source letter (W/B/T/D) in that source's color.
    - `TIER` = tier name in tier color.
    - `N locs` = location count, right-justified. WHITE text. Omitted if 0 or 1 locations.

**Capacity:** Lines 3-16 = 14 lines / 2 lines per device = **7 devices per page**.

**Line 19 — Scroll indicators:**
- Show only when there are more devices than fit on one page.
- ` ^ UP     DOWN v` in GRAY when scrolling is possible in both directions.
- ` ^ UP              ` when on the last page (can only go up).
- `           DOWN v  ` when on the first page (can only go down).
- Hidden when all devices fit on one page.

**Page counter** on line 1: `1/03` means page 1 of 3. Pages are numbered starting at 1.

### Sorting

Devices are presented in descending persistence score order. The most suspicious devices appear on page 1. Devices with no window_flags set (appearance in only one time window) are excluded from this list — they are not interesting enough to show.

### Scroll State

```c
/* Add to display module state */
static uint16_t s_device_list_page = 0;
#define DEVICES_PER_PAGE 7
```

UP increments page (toward less suspicious), DOWN decrements (toward most suspicious). Wrapping: stop at first/last page, do not wrap.

### Data Source

The display task needs to query the device table directly for this screen:

```c
/* Callback fills a sorted array of device_record_t pointers */
void device_table_get_sorted(device_record_t **out, uint16_t max,
                             uint16_t *out_count);
```

Sort by `window_flags` popcount descending, then by `appearance_count` descending. The analysis task can pre-sort and cache this every 2-second cycle.

---

## Screen 3: Disguise Mode (Stealth)

When disguise mode is active, the display shows a plausible pedometer/fitness tracker interface. This screen completely replaces the normal UI. It does not cycle with MODE — MODE is repurposed within disguise mode to look like a fitness app button.

### Entering/Exiting

- **Enter:** Long-press UP+DOWN simultaneously for 3 seconds. Triggers a brief blank flash (100ms) then draws the disguise screen. All scanning continues silently in the background.
- **Exit:** Long-press UP+DOWN simultaneously for 3 seconds. Returns to Screen 0 (Status).
- While in disguise mode, short-press MODE toggles between "steps" and "heart rate" sub-views of the disguise (to make interaction look natural if observed).
- Triple-press MODE while in disguise mode triggers data wipe if configured.

### Layout — Pedometer View

```
+---------------------+
|    14:23   MON      |  <- line 0: time + day
|                     |
|                     |
|                     |
|       4,827         |  <- line 4: step count (large, centered)
|       steps         |  <- line 5: label
|                     |
|   ~~~~~~~~~~~~~~    |  <- line 7: activity graph (fake)
|   ~ ~  ~~~ ~ ~~    |  <- line 8: activity graph
|                     |
|   2.4 mi   187 cal  |  <- line 10: distance + calories
|                     |
|   GOAL: 62%         |  <- line 12: progress
|   [============   ] |  <- line 13: progress bar
|                     |
|                     |
|                     |
|   BAT: 78%          |  <- line 17: battery (real value, looks normal)
|                     |
|                     |
+---------------------+
```

### ASCII Art Mockup

```
+---------------------+
|    14:23   MON       |
|                     |
|                     |
|                     |
|       4,827         |
|       steps         |
|                     |
|   ~~~~~~~~~~~~~~    |
|   ~ ~  ~~~ ~ ~~    |
|                     |
|   2.4 mi   187 cal  |
|                     |
|   GOAL: 62%         |
|   [============   ] |
|                     |
|                     |
|                     |
|   BAT: 78%          |
|                     |
|                     |
+---------------------+
```

### Element Details

**Line 0 — Time and day:**
- `    HH:MM   DAY` — centered. WHITE text, BLACK background.
- Time is real (from GPS/system clock). Day is real. This makes the display genuinely useful and reinforces the disguise.
- Dynamic (time updates).

**Line 4 — Step count:**
- Large centered number. GREEN text.
- **Fake but plausible.** Generated from `(epoch_seconds / 7) % 12000 + 1000`. Slowly increments throughout the day. Resets near midnight. Range: 1,000 - 13,000.
- Dynamic (increments slowly).

**Line 5 — Label:**
- `       steps` — GRAY text. Static.

**Lines 7-8 — Activity graph:**
- Fake waveform using `~` and space characters. CYAN text.
- Regenerated from a simple hash of the current hour so it changes occasionally but not suspiciously fast.
- Semi-static (changes hourly).

**Line 10 — Distance and calories:**
- Derived from fake step count: distance = steps * 0.0005 miles, calories = steps * 0.04.
- WHITE text. Dynamic (derived from step count).

**Lines 12-13 — Goal progress:**
- `GOAL: ##%` where percentage = min(100, steps / 80). GREEN text.
- Progress bar: `[` + `=` repeated for filled + space for unfilled + `]`. GREEN bars, GRAY unfilled.
- Dynamic.

**Line 17 — Battery:**
- `   BAT: ##%` — real battery percentage. GREEN/YELLOW/RED per actual level.
- This is the one real data element. A fitness device showing battery is completely normal.

### Disguise Mode — Heart Rate View (MODE toggle)

```
+---------------------+
|    14:23   MON       |
|                     |
|                     |
|                     |
|        72           |
|        BPM          |
|                     |
|   /\  /\  /\  /\   |
|  /  \/  \/  \/  \  |
|                     |
|   RESTING           |
|   avg today: 68     |
|                     |
|                     |
|                     |
|                     |
|                     |
|   BAT: 78%          |
|                     |
|                     |
+---------------------+
```

**Line 4**: Fake BPM. `60 + (epoch_seconds % 20)`. Range: 60-79. GREEN text.
**Lines 7-8**: Fake ECG waveform using `/\` characters. RED text.
**Line 10**: `   RESTING` — GREEN text. Static.
**Line 11**: `   avg today: ##` — WHITE text. Fake average = BPM - 4.

### Disguise Plausibility

The disguise works because:
1. The T-Display-S3 looks like a generic fitness band or IoT sensor.
2. Step counts and heart rate are mundane data that do not invite inspection.
3. The battery percentage is real, so it matches the device's actual state.
4. The time is real, making it functional as a clock.
5. The 3 buttons map plausibly to a fitness tracker (mode toggle, start/stop, backlight).
6. All CYT-NG scanning continues in the background. No data is lost.

### Disguise State

```c
static bool s_disguise_active = false;
static uint8_t s_disguise_subview = 0; /* 0=pedometer, 1=heart rate */
```

When `s_disguise_active` is true:
- `display_next_screen()` toggles `s_disguise_subview` instead of cycling screens.
- `display_update()` calls `render_disguise_screen()` regardless of `s_current_screen`.
- BLE companion advertising is stopped (`ble_companion_stop_advertising()`).
- Buzzer alerts are suppressed. Display does not auto-switch to alert screen.
- Data collection continues unchanged.

---

## Header Bar (Shared Across Screens 0-2)

The header bar on line 0 is identical across all three normal screens. It provides persistent status at a glance even when switching screens.

```
Format:  " CYT-NG  HH:MM xPR"
         |______||_____||___|
         app name  time  indicators
```

### Indicator Characters (right-justified, positions 18-20)

| Position | Character | Meaning | Color |
|----------|-----------|---------|-------|
| 18 | `.` | No alerts | CYAN (blends with bg) |
| 18 | `*` | Alerts present | RED on CYAN |
| 19 | `P` | Phone connected | WHITE on CYAN |
| 19 | ` ` | Phone not connected | CYAN (invisible) |
| 20 | `R` | Recording active | WHITE on CYAN |
| 20 | ` ` | Not recording | CYAN (invisible) |

The indicators use single characters to be visible to the device holder but unreadable to a bystander.

---

## Alert Behavior

### Alert Arrival While Viewing Screen 0 (Status)

- Alert count and top-device line update automatically on the 2-second refresh.
- No forced screen switch for INFORMATIONAL, NOTABLE, or ELEVATED.
- For REVIEW tier (score >= 0.85): auto-switch to Screen 1 (Alert Detail). Single 50ms buzz from piezo.

### Alert Arrival While Display Is Dimmed

| Tier | Behavior |
|------|----------|
| INFORMATIONAL | No action. User sees it when they wake the display. |
| NOTABLE | No action. User sees it when they wake the display. |
| ELEVATED | Single 50ms buzz. Display stays off. |
| REVIEW | Wake display for 5 seconds, show Alert Detail screen, then re-dim. Single 50ms buzz. |

### Alert Arrival While in Disguise Mode

All tiers: no visible change to the disguise display. No buzzer. Data is logged normally. The user will see alerts when they exit disguise mode. This is the correct behavior — the entire point of disguise mode is that the device must appear innocuous under all conditions.

---

## Display Update Strategy

### Static vs. Dynamic Elements

| Element | Update Frequency | Technique |
|---------|-----------------|-----------|
| Title bar background | Once at screen switch | `draw_text_line` full bar |
| Divider lines | Once at screen switch | `draw_text_line` |
| Nav hints (line 19) | Once at screen switch | `draw_text_line` |
| Device counts | Every 2s | `draw_text_line` per line |
| Alert count/text | Every 2s | `draw_text_line` per line |
| GPS/Battery | Every 2s | `draw_text_line` per line |
| Time in header | Every 2s | `draw_text` at specific cols |
| Indicators in header | Every 2s | `draw_char` at positions 18-20 |
| Device list entries | On page change | Full redraw lines 3-16 |

### Partial Redraw

To keep refresh under 50ms:
1. On screen switch: full clear + draw all lines.
2. On 2-second update: redraw only lines with dynamic content. Skip lines that have not changed (compare with previous buffer).
3. Device list page changes: redraw lines 1-19 only.

### Line Change Detection

```c
static char s_prev_lines[LINES_PER_SCREEN][CHARS_PER_LINE + 1];
```

Before drawing a line, compare with `s_prev_lines[line]`. Skip the draw if identical. This reduces SPI bus traffic by ~60% during steady-state operation.

---

## display_status_t Extensions

The existing struct needs additional fields to support the full UI:

```c
typedef struct {
    /* Existing fields */
    uint32_t total_devices;
    uint32_t suspicious_count;
    uint32_t wifi_count;
    uint32_t ble_count;
    uint32_t tpms_count;
    uint32_t drone_count;
    float    highest_persistence;
    char     highest_device_id[20];
    bool     gps_fix;
    uint8_t  battery_percent;
    bool     sd_ready;
    bool     session_active;

    /* New fields for full UI */
    uint8_t  highest_source_type;       /* source_type_t */
    uint32_t highest_first_seen;        /* epoch seconds */
    uint32_t highest_last_seen;         /* epoch seconds */
    uint8_t  highest_location_count;    /* distinct GPS locations */
    uint32_t session_duration_s;        /* seconds since session start */
    bool     companion_connected;       /* BLE companion connected */
    uint32_t epoch;                     /* current time for display */
} display_status_t;
```

---

## Data Wipe (Triple-Press MODE)

Triple-press MODE (three presses within 1 second) while in disguise mode triggers:

1. Immediate stop of session recording.
2. Delete all files under `/sdcard/` (SD card wipe).
3. Zero-fill the device table in PSRAM.
4. Reset `display_status_t` to all zeros.
5. Display shows the disguise pedometer screen with no indication that a wipe occurred.

This provides the "duress" capability recommended by the social-psych review. The device continues to function as a pedometer-looking gadget with no evidence of CYT-NG data.

If no SD card is present, steps 2 is skipped. The wipe completes in under 500ms. No confirmation prompt (the triple-press in disguise mode is already a deliberate, hard-to-trigger-accidentally gesture).

---

## Screen Transition Summary

```
                    MODE (short)
     +---------+  ------------>  +---------+  ------------>  +---------+
     | Screen 0|                 | Screen 1|                 | Screen 2|
     | Status  |  <------------  | Alert   |  <------------  | Devices |
     +---------+    MODE (short) +---------+    MODE (short) +---------+
          |              |              |
          |   UP+DOWN    |   UP+DOWN    |   UP+DOWN
          |   (3s hold)  |   (3s hold)  |   (3s hold)
          v              v              v
     +---------+
     | Screen 3|    MODE (short) = toggle pedometer/HR subview
     | Disguise|    UP+DOWN (3s hold) = exit back to Screen 0
     +---------+
```

MODE cycles 0 -> 1 -> 2 -> 0. It never enters Screen 3 via MODE.
Screen 3 is a modal overlay entered/exited only via the UP+DOWN combo.
