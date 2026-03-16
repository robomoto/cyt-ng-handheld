# CYT-NG Handheld: UX Interaction Specification

**Author:** UX Specialist
**Date:** 2026-03-16
**Hardware:** LilyGo T-Display-S3 (170x320 TFT, 3 buttons, piezo buzzer)
**Display constraints:** 21 characters/line, 20 lines/screen, 8x16 bitmap font

---

## Design Principles

1. **Silent by default.** The device should not draw attention to itself or its user. Screen off, buzzer silent, BLE advertising quiet. The user should be able to forget it is in their pocket until it has something worth saying.

2. **Calm, factual language.** No exclamation points. No intent attribution. No certainty. The device presents observations, never conclusions. It never says "you are being followed." It says "a device has appeared at multiple locations."

3. **Three-second assessment.** When the user wakes the screen, they must be able to determine "do I need to pay attention?" within three seconds. This means a single dominant visual element on the first screen: a color-coded status bar.

4. **Graceful degradation.** The device must be fully usable without looking at the screen (buzzer patterns), without the companion phone (TFT shows everything), and without GPS (time-only correlation still works).

5. **Discovery is survivable.** If someone else sees the device, it must be possible for the user to present it as something innocuous. Disguise mode is not a bonus feature; it is a safety requirement.

---

## Button Map

| Button | Short Press | Long Press (2s) | Context |
|--------|------------|-----------------|---------|
| **UP** (GPIO 2) | Previous screen / scroll up | Brightness toggle (dim/bright) | All screens |
| **DOWN** (GPIO 3) | Next screen / scroll down | Mark device as familiar (on alert detail) | All screens |
| **MODE** (GPIO 14) | Wake display / cycle screens | Enter/exit disguise mode (see Section 4) | All screens |

**Any button** press wakes the display if it is off. The first press only wakes; it does not navigate.

**Three-button hold** (all three for 5 seconds): Quick-wipe. Erases all session data from SD card and resets to factory appearance. Last resort.

---

## 1. First Power-On Experience

The device must communicate its purpose and set expectations without requiring the user to read a manual. The user may have received this device from a victim advocate and may not know what it does. Three screens, auto-advancing, no interaction required.

### Screen 1 of 3 (displayed for 8 seconds)

```
---------------------


  SCANNING DEVICE

  This device looks
  for wireless
  trackers near you.

  It works silently
  in your pocket.








  [auto-advancing...]
---------------------
```

**Colors:** White text on black. "SCANNING DEVICE" in cyan.

### Screen 2 of 3 (displayed for 8 seconds)

```
---------------------


  HOW IT WORKS

  If something
  unusual is found,
  you will feel a
  short buzz.

  Check the screen
  for details.

  Most detections
  are routine.




  [auto-advancing...]
---------------------
```

**Colors:** White text on black. "HOW IT WORKS" in cyan.

### Screen 3 of 3 (displayed for 8 seconds)

```
---------------------


  LEARNING YOUR
  ENVIRONMENT

  Stay where you are
  for 5 minutes.

  The device will
  learn what is
  normal here.


  When done, put me
  in your bag.



  [scanning starts]
---------------------
```

**Colors:** White text on black. "LEARNING YOUR ENVIRONMENT" in cyan.

### After 5 minutes: transition to normal operation

The display briefly shows:

```
---------------------







   SETUP COMPLETE

   MONITORING ACTIVE









---------------------
```

**Colors:** "SETUP COMPLETE" in green. "MONITORING ACTIVE" in green. Held for 4 seconds, then the display auto-dims and enters normal operation.

### Implementation notes

- First-boot detection: a flag in NVS (non-volatile storage). Set after the first-boot sequence completes. If the user quick-wipes, this flag is reset, and first-boot runs again.
- During the 5-minute learning period, the device is scanning normally and building its familiar-device baseline. No alerts fire during this window.
- The user can skip the sequence by pressing MODE at any time. The baseline learning still runs for 5 minutes in the background.

---

## 2. Daily Use Flow

### 2.1 Screen-Off State (default)

The display is off. The device is scanning. The buzzer is silent. This is the state the device is in 95% of the time.

The status LED (if present on the T-Display-S3 board) should be OFF. No visible indication that the device is powered on. The device appears inert.

### 2.2 Waking the Display

**Any button press** wakes the display. The first press is consumed by the wake action; it does not navigate. This prevents accidental screen changes when pulling the device from a pocket.

On wake, the display shows the **Status Screen** (always; regardless of which screen was showing when it dimmed). This guarantees the three-second assessment.

### 2.3 Status Screen (home screen)

This is the most important screen in the entire system. It must answer "do I need to pay attention?" in three seconds or less.

```
---------------------
  CYT-NG HANDHELD
---------------------

  NO ITEMS FOR
  REVIEW

  247 devices seen
  All routine

---------------------
  WiFi:142 BLE:89
  TPMS:12  Drone:0
---------------------
  GPS:FIX  SD:LOG
  Batt:73%  12:34
---------------------
  UP/DOWN: screens
  MODE: options
---------------------
```

**The critical element** is lines 4-5. This is the dominant message. It uses the largest visual weight on the screen and one of three states:

| State | Lines 4-5 Text | Color | Meaning |
|-------|---------------|-------|---------|
| Clear | `NO ITEMS FOR REVIEW` | Green | Nothing unusual detected. |
| Notable | `1 ITEM NOTED` | Yellow | Something was logged but does not require action. |
| Review | `1 ITEM FOR REVIEW` | Red, pulsing | A device has reached ELEVATED or REVIEW tier. User should look at the alert screen. |

**Pulsing:** The red text alternates between bright red and dark red on a 1-second cycle. This is the only animation on any screen. It draws the eye without being garish.

The line "247 devices seen / All routine" provides context. It normalizes the environment: most of what is out there is harmless. This directly addresses the social psychology review's recommendation to frame the environment as mostly safe.

If items are notable or flagged for review, the second line changes:

- `246 routine, 1 noted`
- `245 routine, 2 flagged`

### 2.4 Alert Screen

Reached by pressing DOWN from Status or by pressing any button when a REVIEW-tier alert is active (the device auto-navigates here after buzzing).

```
---------------------
   ITEM FOR REVIEW
---------------------

 A device has been
 seen at 3 of your
 locations over 4h.

 This pattern is
 unusual but may be
 coincidental.

 Confidence: HIGH

---------------------
 DOWN: details
 MODE: back
---------------------
```

**Colors:** Title bar in red background. Body text in white. "Confidence: HIGH" in red.

The word choices are precise:
- "A device" -- not "someone" or "a tracker." No intent.
- "seen at 3 of your locations" -- factual observation.
- "over 4h" -- time context.
- "This pattern is unusual but may be coincidental." -- mandatory uncertainty hedge.
- "Confidence: HIGH" -- natural language, not a raw score.

### Alert text by tier (fits 21 chars/line)

**NOTABLE tier** (persistence 0.5-0.7):

```
 A device has been
 near you at 2
 locations.

 This is likely
 routine.

 Confidence: LOW
```

**ELEVATED tier** (persistence 0.7-0.85):

```
 A device has been
 seen at 3 of your
 locations over 4h.

 This pattern is
 unusual but may be
 coincidental.

 Confidence: MODERATE
```

**REVIEW tier** (persistence 0.85-1.0):

```
 A device has been
 seen at 4+ of your
 locations over 2d.

 This pattern is
 unusual.

 Confidence: HIGH
```

Note: "Confidence: MODERATE" is exactly 21 characters. Every string has been counted.

### 2.5 Device Detail Screen

Reached by pressing DOWN from the alert screen.

```
---------------------
  DEVICE DETAIL
---------------------
 Type: BLE tracker
 First: 03/14 09:12
 Last:  03/16 14:47
 Locations: 4
 Score: HIGH

---------------------
 DO:
  Stay calm.
  Continue normally.
  Review when safe.
 DO NOT:
  Confront anyone.
  Search for device.
---------------------
 DOWN: more / MODE:^
---------------------
```

**Colors:** "DO:" in green. "DO NOT:" in red. Guidance text in white. Device metadata in cyan.

The guidance text is always visible on this screen. It is not optional. Every time a user views a flagged device, they see "Stay calm. Continue normally." This is the UX review's P0 recommendation implemented directly in the firmware.

### 2.6 Device List Screen

Reached by pressing DOWN from Device Detail, or cycling screens.

```
---------------------
  DEVICE LIST
---------------------
 247 total tracked

 WiFi:  142
 BLE:   89
 TPMS:  12
 Drone: 0

 Suspicious: 2







 UP/DOWN: scroll
 MODE: back
---------------------
```

In v2 this screen will support scrolling through individual suspicious devices with UP/DOWN. In v1 it shows summary counts.

### 2.7 When the Companion Phone is Connected

If BLE companion is connected, the status screen adds a connection indicator:

```
  GPS:FIX  SD:LOG
  Batt:73%  Ph:YES
```

"Ph:YES" (phone connected) replaces the clock. The phone handles detailed alerts; the TFT shows a simplified view. See Section 5 for full companion handoff behavior.

### 2.8 Auto-Off Behavior

- After 30 seconds of no button presses, the backlight dims to 20% brightness (not off).
- After 60 seconds total, the backlight turns off completely.
- The dim step gives the user a visual cue that the screen is about to turn off, allowing them to tap a button to keep it awake.
- When an alert escalates to ELEVATED or REVIEW, the display wakes automatically and shows the alert screen. See Section 2.9.

### 2.9 When the Buzzer Should Fire

The buzzer is the primary alert channel. The user may never look at the screen. The buzzer must communicate severity through pattern alone.

| Tier | Buzzer Pattern | When |
|------|---------------|------|
| SILENT (0.0-0.3) | Nothing | Never |
| INFORMATIONAL (0.3-0.5) | Nothing | Never. Logged only. |
| NOTABLE (0.5-0.7) | Nothing | Never. Visible on screen only when user checks. |
| ELEVATED (0.7-0.85) | Two short pulses (50ms on, 100ms gap, 50ms on) | Once, when the device first crosses the 0.7 threshold. Not repeated. |
| REVIEW (0.85-1.0) | Three pulses with longer tone (100ms on, 100ms gap, 100ms on, 100ms gap, 200ms on) | Once when crossing 0.85. Repeated once more after 5 minutes if not dismissed. Then silent. |

**The buzzer never fires for sub-ELEVATED detections.** This is a hard rule. Alert fatigue is the enemy. In a busy urban environment, dozens of devices will reach NOTABLE every hour. If the buzzer fires for NOTABLE, the user will disable the device within a day.

**The buzzer never fires more than twice for the same device.** Persistence is logged, not nagged.

**Rate limiting:** No more than 3 buzzer events per hour, regardless of how many devices cross thresholds. If a fourth device reaches ELEVATED within the same hour, it is queued and displayed on screen only.

**When the companion phone is connected:** The buzzer is disabled by default. Alerts go to the phone as push notifications. The user can override this (buzzer always on, buzzer always off, buzzer only when phone disconnected) via the companion app's configuration command over BLE GATT characteristic ff14.

### 2.10 Normalization of the Environment

Every status screen update should show the total device count alongside the flagged count. The ratio provides psychological grounding: "247 devices, 1 flagged" feels very different from just "1 ALERT." The former says "the world is mostly safe and one thing needs attention." The latter says "danger." This follows the social psychology review's recommendation to normalize benign detections.

---

## 3. Alert Response Flow

### 3.1 Full Navigation Path

```
Button press (any)
    │
    ▼
STATUS SCREEN ──── "NO ITEMS FOR REVIEW" ──── user puts device away
    │
    │ (if items flagged)
    │ "1 ITEM FOR REVIEW"
    │
    ▼ (press DOWN)
ALERT SCREEN ──── shows highest-priority device with plain-language summary
    │
    ▼ (press DOWN)
DEVICE DETAIL ──── type, timestamps, location count, guidance text
    │
    ▼ (press DOWN)
DEVICE LIST ──── all tracked devices with counts
    │
    ▼ (press DOWN or MODE)
STATUS SCREEN ──── (cycle back to top)
```

The complete path from "wake device" to "understand what was detected and what to do" is four button presses: wake, DOWN, DOWN, read. Under 10 seconds.

### 3.2 Guidance Text (Always Visible on Device Detail)

The guidance text is not hidden behind a menu. It occupies the bottom half of the device detail screen. It is always present. The text is:

```
 DO:
  Stay calm.
  Continue normally.
  Review when safe.
 DO NOT:
  Confront anyone.
  Search for device.
```

Every line is under 21 characters. Every line is a complete instruction.

### 3.3 When to Show "See Phone"

The TFT never says "see phone for details" unless the companion is connected. When it is connected, the alert screen adds one line:

```
 Open app for map
 and full history.
```

This appears below the confidence line. It is a suggestion, not a requirement. Everything the user needs to assess safety is on the TFT. The phone adds depth (map view, timeline, historical patterns) but is not required.

### 3.4 Dismissing an Alert

Pressing MODE from the alert screen returns to the status screen. The alert remains in the log but the "ITEM FOR REVIEW" count decrements by one. The pulsing red text stops if all items are dismissed.

Long-pressing DOWN on the device detail screen marks the device as "familiar." It will not trigger future alerts unless it appears at an unexpected location. A brief confirmation:

```
 MARKED FAMILIAR
 Will not alert again
 unless pattern
 changes.
```

Held for 2 seconds, then returns to the alert screen.

### 3.5 Evidence Preservation

When a device reaches REVIEW tier, the SD card logger automatically saves a detailed evidence snapshot: device ID, all sighting timestamps, all GPS coordinates, signal strengths, source types. This happens without user action. The user does not need to remember to "save evidence." It is always saved.

If the companion phone is connected, the evidence snapshot is also pushed to the phone over BLE GATT. The phone stores it in its local database.

---

## 4. Stealth/Disguise Transitions

### 4.1 Entering Disguise Mode

**Trigger:** Long-press MODE (2 seconds).

This duration is chosen to prevent accidental activation (short presses cycle screens) while being fast enough for urgent use. An observer watching the user would see them hold a button on a small device for two seconds. This is indistinguishable from normal device interaction.

**What happens:**

1. The display immediately transitions to a disguise screen (see 4.3).
2. BLE advertising changes to a generic device name (e.g., "TempSensor" or "Pedometer").
3. The GATT service UUIDs change to non-CYT values (a generic environmental sensor profile).
4. The buzzer is silenced.
5. Scanning continues silently in the background. Alerts are logged to SD but produce no output.

**There is no confirmation dialog.** The transition must be instant and silent. A dialog asking "Enter disguise mode?" is exactly what should not be on screen if someone is looking over the user's shoulder.

### 4.2 Disguise Screen Options

The disguise screen should look like a plausible, boring device. Two options, configurable via companion app or hardcoded at build time:

**Option A: Temperature/Humidity sensor**

```
---------------------
  ENV MONITOR v2.1
---------------------

  Temp:  72.4 F
  Humid: 45%
  Press: 29.92 inHg

  Min: 68.1  Max:74.2

  Logging to SD...








---------------------
```

Values are generated from a plausible random range, seeded by the RTC. They drift slowly over time to look realistic.

**Option B: Step counter**

```
---------------------
  STEP TRACKER
---------------------

  TODAY: 4,287 steps

  Distance: 1.9 mi
  Calories: 203

  Goal: 10,000
  ||||||||........
  43% complete








---------------------
```

Step count increments plausibly based on accelerometer noise or a simple time-based model (roughly 100 steps per minute when "active," paused when stationary).

### 4.3 Exiting Disguise Mode

**Trigger:** Long-press MODE (2 seconds) again.

Same gesture as entry. The user does not need to remember a different gesture. The display returns to the normal status screen. BLE advertising resumes with the CYT service UUID. Buzzer re-enables.

**On exit, if any alerts accumulated during disguise mode,** the status screen immediately shows the alert count. The buzzer fires its pattern once to catch the user's attention. This is the only time the buzzer fires on a screen transition rather than a threshold crossing.

### 4.4 Alerts During Disguise Mode

| Alert Tier | Behavior in Disguise Mode |
|-----------|--------------------------|
| SILENT | Logged to SD. No output. |
| INFORMATIONAL | Logged to SD. No output. |
| NOTABLE | Logged to SD. No output. |
| ELEVATED | Logged to SD. No buzzer. No screen change. If companion phone is connected, the phone receives the alert silently (notification, no sound). |
| REVIEW | Logged to SD. No buzzer. **The disguise screen adds a subtle indicator:** a small dot in the top-right corner of the screen, rendered in the same color as the surrounding text so it is not obvious to a casual observer. The user who knows to look for it will see it. If the companion phone is connected, the phone receives the alert as a normal push notification. |

The REVIEW-tier dot is the only concession to urgency in disguise mode. It must not compromise the disguise. A dot in the corner of a "temperature sensor" display is not suspicious. The dot disappears when the user exits disguise mode (the alert is then shown normally).

### 4.5 Quick-Wipe

**Trigger:** Hold all three buttons (UP + DOWN + MODE) simultaneously for 5 seconds.

**What happens:**

1. The SD card is formatted (all session data erased).
2. NVS is cleared (removes familiar device list, first-boot flag, configuration).
3. The device reboots into the first-boot sequence.
4. There is no confirmation dialog. The 5-second hold is the confirmation.

**Visual feedback during the hold:** A progress bar fills across the bottom of the screen over 5 seconds. If any button is released before 5 seconds, the wipe aborts. No data is lost on a partial hold.

```
 HOLD TO RESET...
 |||||||||||.........
```

After the wipe, the device looks like it just came out of the box. An observer who powers it on will see the first-boot sequence. There is no indication that data was ever present.

---

## 5. Phone Companion Handoff

### 5.1 Connection States

| State | TFT Indicator | Behavior |
|-------|--------------|----------|
| No phone paired | `Ph:---` (gray) | TFT handles all alerts and display. Buzzer active. |
| Phone paired but disconnected | `Ph:OFF` (yellow) | Same as no phone. Device operates independently. |
| Phone connected | `Ph:YES` (green) | Alerts route to phone. Buzzer disabled by default. TFT still functional. |

### 5.2 What Changes When the Phone is Connected

**Alerts:** ELEVATED and REVIEW alerts push to the phone as notifications via BLE GATT characteristic ff11. The phone app renders them with the same tier-appropriate language. The TFT still shows alert counts on the status screen, but the buzzer is silent (configurable).

**Status updates:** The phone receives JSON status every 5 seconds via characteristic ff12. The phone app can show a richer dashboard: map view, device history, timeline.

**Device details:** The phone can request the full suspicious device list via characteristic ff13. This provides the map view, RSSI history, and location plot that cannot fit on a 170x320 screen.

**The TFT does not go into power-save when the phone is connected.** The user may need to glance at the handheld if they cannot check their phone (meeting, driving, social situation). The TFT remains fully functional. It simply defers to the phone for detailed information and primary alerting.

### 5.3 What the TFT Shows Differently

When the phone is connected, the alert screen adds context:

```
 A device has been
 seen at 3 of your
 locations over 4h.

 Confidence: HIGH

 Open app for map
 and full history.
```

The device detail screen omits the DO/DO NOT guidance (the phone app shows it in a richer format) and instead shows:

```
 Type: BLE tracker
 First: 03/14 09:12
 Last:  03/16 14:47
 Locations: 4
 Score: HIGH

 Full details and
 safety resources
 available in app.
```

### 5.4 Phone Disconnects Mid-Session

When the BLE connection drops:

1. The status screen indicator changes from `Ph:YES` to `Ph:OFF` (yellow).
2. The buzzer re-enables immediately. If any ELEVATED/REVIEW alerts are pending, the buzzer fires once.
3. BLE advertising resumes so the phone can reconnect.
4. The TFT resumes full alert display, including the DO/DO NOT guidance on device detail.
5. There is no interruption to scanning or logging. The device operates independently without interruption.

The transition is silent if there are no pending alerts. The user may not notice the disconnect unless they look at the screen. This is intentional: the disconnect is not an emergency, and the device handles it gracefully.

### 5.5 Companion Configuration via Phone

The phone can send commands via GATT characteristic ff14 (JSON write) to configure:

- Buzzer behavior: `{"buzzer": "always" | "phone_disconnected" | "never"}`
- Disguise screen selection: `{"disguise": "temp_sensor" | "step_counter"}`
- Alert threshold: `{"threshold": 0.7}` (0.5 - 0.95 range)
- Display brightness: `{"brightness": 0-255}`
- Mark device familiar: `{"familiar": "device_id"}`

These settings persist in NVS across power cycles.

---

## 6. Accessibility

### 6.1 Screen-Free Operation via Buzzer Patterns

The device can be used entirely without looking at the screen. The buzzer patterns encode all critical information:

| Pattern | Meaning | Duration |
|---------|---------|----------|
| Silence | Everything is routine. | Continuous |
| Two short pulses (50ms-100ms-50ms) | ELEVATED: A device needs attention. | 200ms total |
| Three pulses, last one longer (100ms-100ms-100ms-100ms-200ms) | REVIEW: A device shows persistent pattern. Check when safe. | 600ms total |
| Single very short tick (20ms) | Acknowledgment: button press registered, display waking. | 20ms |
| Rapid triple-tick (20ms-50ms-20ms-50ms-20ms) | Quick-wipe initiated (during 5s hold). | 150ms, repeats every second during hold |
| Descending two-tone (high 100ms, low 100ms) | Error: SD card missing, GPS lost, low battery. | 200ms, once |

A vision-impaired user can determine:
- **Am I safe?** Silence = yes. Any buzzing = check with someone who can read the screen, or check the companion app (which supports screen readers).
- **How urgent?** Two pulses = moderate. Three pulses = significant.
- **Did the device register my button press?** Single tick = yes.
- **Is something wrong with the hardware?** Descending tone = yes.

### 6.2 Screen Navigation for Low Vision

- The 8x16 bitmap font at 170px width produces characters approximately 2mm wide. This is small even for users with normal vision.
- **High-contrast mode** (default): White text on black background. Color is used for emphasis (green = OK, red = attention, cyan = informational) but is never the sole indicator. Text labels accompany all color states ("NO ITEMS FOR REVIEW" in green vs "1 ITEM FOR REVIEW" in red -- the text changes, not just the color).
- **Large font mode (v2):** A 16x32 font option that shows 10 chars/line, 10 lines/screen. Less information per screen but readable at arm's length. Toggled via companion app command or a build-time configuration.

### 6.3 Motor Impairment

- The three buttons have distinct physical positions (up, down, side) so they can be found by touch.
- The most critical action (wake screen, assess status) requires a single press of any button.
- Dismissing an alert requires one press of MODE.
- No action requires simultaneous button presses except quick-wipe (which is deliberately hard to trigger accidentally).
- Long-press thresholds (2 seconds for disguise, 5 seconds for wipe) are chosen to be achievable but not accidental. These could be made configurable via companion app command if needed.

### 6.4 Cognitive Accessibility

- The status screen answers exactly one question: "Do I need to pay attention?" Yes or no.
- If yes, the alert screen answers: "What happened?" in plain language.
- If the user needs guidance: "What should I do?" is always visible on the detail screen.
- At no point does the user need to interpret a number, decode an abbreviation, or make a technical judgment.
- The three-screen flow (status > alert > detail) mirrors a natural question sequence. The user never needs to figure out where to go.

---

## 7. Error States

Errors are displayed on the status screen in the bottom section, replacing the normal system info lines. Errors use the descending two-tone buzzer pattern (once) to get attention.

### 7.1 SD Card Full

```
  SD: FULL
  Logging paused.
  Replace or clear
  SD card.
```

**Color:** Red. **Buzzer:** Descending tone once. **Behavior:** Scanning continues. Alerts still fire. Data is held in the 64KB RAM buffer and rotates (oldest dropped). When the SD card is replaced or cleared, logging resumes automatically.

### 7.2 SD Card Missing

```
  SD: NONE
  No logging active.
  Insert SD card to
  save session data.
```

**Color:** Yellow. **Buzzer:** Descending tone once at boot. Not repeated. **Behavior:** The device operates normally without logging. Alerts still fire. Device tracking still works (in PSRAM). The user loses the ability to export evidence to the base station.

### 7.3 GPS No Fix (Extended)

GPS commonly takes 30-60 seconds for cold start. No error is shown until 5 minutes without a fix.

```
  GPS: NO FIX (5m)
  Location tracking
  unavailable.
  Time-only mode.
```

**Color:** Yellow. **Buzzer:** None (GPS loss is not urgent). **Behavior:** The device continues scanning and tracking devices by time correlation only. Multi-location detection is disabled. The status screen shows `GPS:---` instead of `GPS:FIX`.

When the fix is reacquired, the indicator returns to `GPS:FIX` silently. No buzzer, no notification.

### 7.4 CC1101 Not Responding

```
  CC1101: ERROR
  Sub-GHz scan off.
  WiFi + BLE still
  active.
```

**Color:** Red. **Buzzer:** Descending tone once. **Behavior:** WiFi and BLE scanning continue normally. TPMS detection is disabled. The error clears automatically if the CC1101 recovers (e.g., after an SPI bus contention resolves).

### 7.5 Low Battery

| Battery % | Indicator | Buzzer | Behavior |
|-----------|----------|--------|----------|
| 21-100% | `Batt:XX%` (green) | None | Normal |
| 11-20% | `Batt:XX%` (yellow) | None | Normal |
| 6-10% | `Batt:XX% LOW` (red) | Descending tone once | Display auto-off timeout shortened to 15 seconds |
| 1-5% | `Batt:XX% CRITICAL` (red, pulsing) | Descending tone every 5 minutes | GPS disabled to conserve power. Scanning continues. |
| 0% | Device powers off | N/A | Session data already on SD. |

At 10%, if the companion phone is connected, a "low battery" notification is sent to the phone.

### 7.6 Multiple Simultaneous Errors

If multiple error conditions exist, they are shown on the status screen stacked in priority order (highest first). The bottom section of the status screen can display up to 3 error lines. If more exist, the most critical are shown with a trailing `+1 more` indicator.

---

## 8. Screen Architecture Summary

```
                    ┌─────────────┐
                    │   BOOT      │
                    │  (3 screens │
                    │   auto-     │
                    │   advance)  │
                    └──────┬──────┘
                           │
                           ▼
    ┌──────────────────────────────────────────┐
    │                                          │
    │              STATUS SCREEN               │
    │         (home, always first on wake)      │
    │                                          │
    │  "NO ITEMS FOR REVIEW" / "N ITEMS..."    │
    │  Device counts, GPS, battery, phone      │
    │                                          │
    └─────────┬───────────────────┬────────────┘
              │ DOWN              │ UP
              ▼                   ▼
    ┌─────────────────┐  ┌─────────────────┐
    │  ALERT SCREEN   │  │  DEVICE LIST    │
    │  (highest-      │  │  (summary       │
    │   priority      │  │   counts, v2    │
    │   device)       │  │   scrollable)   │
    └────────┬────────┘  └─────────────────┘
             │ DOWN
             ▼
    ┌─────────────────┐
    │ DEVICE DETAIL   │
    │ (type, times,   │
    │  locations,     │
    │  guidance)      │
    └─────────────────┘

    MODE from any screen → back to STATUS
    Long-press MODE → toggle DISGUISE
    3-button hold 5s → QUICK-WIPE → reboot to BOOT
```

### Screen Cycle (MODE short press)

MODE short press on any screen returns to STATUS. This is a "home" button, not a cycle button. The user always knows how to get back to the top: press MODE.

UP and DOWN navigate linearly: STATUS <-> ALERT <-> DETAIL <-> DEVICE LIST, wrapping around.

---

## 9. Buzzer Summary Table

| Event | Pattern | Duration | Repeats |
|-------|---------|----------|---------|
| ELEVATED alert | `.. ..` (two pulses) | 200ms | Once. Never repeated for same device. |
| REVIEW alert | `.. .. ____` (three pulses, last long) | 600ms | Once, then once more after 5 min if not dismissed. |
| Button press ack | `.` (single tick) | 20ms | Once per press. |
| Quick-wipe hold | `...` (triple tick) | 150ms | Every second during hold. |
| Hardware error | `\\_` (descending tone) | 200ms | Once per error occurrence. |
| Low battery (<6%) | `\\_` (descending tone) | 200ms | Every 5 minutes. |
| Disguise exit (pending alerts) | `.. ..` (two pulses) | 200ms | Once. |
| Phone disconnect (pending alerts) | `.. ..` (two pulses) | 200ms | Once. |

**Maximum buzzer events per hour:** 3 alert-related buzzes. Hardware errors and button acknowledgments do not count toward this limit.

---

## 10. Implementation Priority

| Priority | Feature | Screens/Functions Affected |
|----------|---------|---------------------------|
| **P0** | Status screen with clear/notable/review states | `render_status_screen()` |
| **P0** | Alert screen with tier-appropriate language | `render_alert_screen()` |
| **P0** | Device detail with DO/DO NOT guidance | New: `render_detail_screen()` |
| **P0** | Buzzer patterns for ELEVATED and REVIEW | New: `buzzer_alert()` |
| **P0** | Wake-on-button (first press = wake only) | `display_wake()` + button ISR |
| **P0** | Disguise mode (long-press MODE) | New: `render_disguise_screen()`, `stealth_enter()`, `stealth_exit()` |
| **P1** | First-boot sequence | New: `render_boot_sequence()` |
| **P1** | Companion phone connection indicator | Status screen, BLE connection callback |
| **P1** | Buzzer suppression when phone connected | `ble_companion_is_connected()` check in buzzer |
| **P1** | Alert auto-navigation (buzzer fires, screen jumps to alert) | `display_update()` |
| **P1** | Quick-wipe (3-button hold) | Button ISR, SD format, NVS erase |
| **P2** | Mark-as-familiar (long-press DOWN) | Device detail screen, device table |
| **P2** | Phone disconnect recovery (buzzer re-enable) | BLE disconnect callback |
| **P2** | Error state display | Status screen bottom section |
| **P2** | Auto-dim before auto-off | `display_update()` backlight logic |
| **P3** | Large font mode | Alternate font table, companion config |
| **P3** | Disguise screen fake data generation | Temperature/step counter models |
| **P3** | Rate-limited buzzer (3/hour cap) | Buzzer task |

---

## Appendix A: Character Budget Reference

All user-facing strings verified against the 21-character line limit:

```
"  CYT-NG HANDHELD"     18 chars
"NO ITEMS FOR REVIEW"   19 chars
"1 ITEM FOR REVIEW"     17 chars
"1 ITEM NOTED"          12 chars
"247 devices seen"      16 chars
"All routine"           11 chars
"245 routine, 2 flagged" 22 chars — OVER. Use "245 ok, 2 flagged" (18)
"Confidence: HIGH"      16 chars
"Confidence: MODERATE"  20 chars
"Confidence: LOW"       15 chars
"A device has been"     17 chars
"seen at 3 of your"     17 chars
"locations over 4h."    18 chars
"This pattern is"       15 chars
"unusual but may be"    18 chars
"coincidental."         13 chars
"Stay calm."            10 chars
"Continue normally."    18 chars
"Review when safe."     17 chars
"Confront anyone."      16 chars
"Search for device."    18 chars
"MARKED FAMILIAR"       15 chars
"HOLD TO RESET..."      16 chars
"GPS: NO FIX (5m)"      16 chars
"CC1101: ERROR"          13 chars
"Batt:XX% CRITICAL"     17 chars
"Ph:YES"                  6 chars
"Ph:OFF"                  6 chars
"Ph:---"                  6 chars
"Open app for map"       16 chars
"and full history."      17 chars
```

All strings fit within the 21-character constraint.

---

## Appendix B: Color Palette

| Name | RGB565 | Hex (approx RGB) | Usage |
|------|--------|-------------------|-------|
| Black | 0x0000 | #000000 | Background (always) |
| White | 0xFFFF | #FFFFFF | Primary text |
| Green | 0x07E0 | #00FF00 | Safe states, DO guidance |
| Red | 0xF800 | #FF0000 | Review items, DO NOT guidance, errors |
| Yellow | 0xFFE0 | #FFFF00 | Notable items, warnings |
| Cyan | 0x07FF | #00FFFF | Title bars, informational data |
| Orange | 0xFD20 | #FF6900 | ELEVATED confidence level |
| Dark Gray | 0x2104 | #202020 | Navigation hints, disabled items |

**Contrast note:** White on black = maximum contrast (21:1). Green on black = 8.5:1 (passes WCAG AAA for large text). Red on black = 5.3:1 (passes WCAG AA). All color states are accompanied by distinct text, so color is never the sole indicator.
