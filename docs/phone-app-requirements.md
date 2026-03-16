# CYT-NG Companion App: Requirements Specification

Requirements for the smartphone companion app. This is a separate project (third repo) that communicates with both the ESP32 handheld and the Raspberry Pi base station.

## Overview

The companion app serves three roles:
1. **Tier 0 (standalone):** BLE tracker detector using the phone's own BLE radio — no hardware needed
2. **Tier 1 (handheld companion):** Display, alerts, and configuration for the ESP32 handheld via BLE GATT
3. **Tier 2 (base station companion):** Session viewer and remote dashboard for the Pi base station via WiFi/HTTP

## Target Platforms

- **Android** (primary) — more BLE scanning freedom, background service support
- **iOS** (secondary) — restricted BLE background modes, but large user base
- **Framework:** Flutter or React Native (cross-platform, single codebase)

## Tier 0: Standalone BLE Tracker Detection

The app runs independently using the phone's own Bluetooth radio. No ESP32 or Pi required.

### Scanning
- Passive BLE advertisement scanning (same classifier logic as the ESP32)
- Detect: Apple Find My (AirTags, AirPods), Samsung SmartTags, Tile, Google Find My Device
- Payload fingerprinting to handle MAC rotation (SHA-256 of manufacturer data)
- Scan interval: configurable (default every 60s for 10s window)
- Background scanning: Android foreground service with persistent notification; iOS background BLE task (limited by Apple)

### Detection Logic
- Port the persistence scoring from the base station: time-window tracking, multi-location correlation
- Familiar device store (user marks known devices)
- Alert threshold: configurable, default 0.7 persistence score

### Alerts
- Five-tier alert language (SILENT/INFORMATIONAL/NOTABLE/ELEVATED/REVIEW) — same as base station
- No certainty language, no intent attribution, no exclamation points
- Response guidance per tier with safety resources (DV hotline, NNEDV, Crisis Text Line, RAINN)
- Standard push notifications — indistinguishable from any other app notification

### Data
- Local SQLite database for device sightings and sessions
- Auto-delete unflagged data after 48 hours
- Retain flagged data for 90 days
- No cloud storage, no account required, no data leaves the phone

## Tier 1: ESP32 Handheld Companion

The app connects to the ESP32 handheld via BLE GATT and acts as its display/notification interface.

### BLE Connection
- Discover ESP32 by service UUID (0000ff10-...)
- Auto-reconnect on disconnect
- Pair/bond for encrypted connection
- Connection status indicator in the app

### Data from ESP32 (read/subscribe)
- **Alert Stream** (characteristic ff11, notify): Real-time JSON alerts from all ESP32 scanners
- **Status** (characteristic ff12, read/notify): Device counts, GPS fix, battery, session state
- **Device List** (characteristic ff13, read): Top suspicious devices with persistence scores

### Commands to ESP32 (write)
- **Command** (characteristic ff14, write): JSON commands for:
  - Start/stop session
  - Mark device as familiar
  - Set scan parameters
  - Request device list refresh
  - Trigger SD card flush

### Display
- Real-time dashboard: device counts per source type (WiFi, BLE, TPMS, Drone)
- Alert feed with tier-appropriate language and response guidance
- Map view: plot device appearances on a map using phone GPS (Leaflet/MapKit/Google Maps)
- Device detail view: appearance history, RSSI over time, locations seen
- Session history: past sessions from SD card imports

### Alerts
- Push notifications for ELEVATED and REVIEW tier alerts from ESP32
- Haptic feedback on watch (if supported)
- Notification text uses the same five-tier language framework
- Tapping notification opens device detail view

## Tier 2: Base Station Companion

The app connects to the Pi base station over WiFi (HTTP/WebSocket).

### Connection
- Discover base station on local network (mDNS: `cyt-ng.local`)
- HTTP REST API for data queries
- WebSocket for real-time alert streaming
- Authentication: API key or password (simple, no cloud auth)

### Features
- Full dashboard with all 9 scanner types
- Cross-source correlation visualization
- Session browser with historical analysis
- Handheld session import viewer (see what the handheld captured)
- KML export trigger
- Scanner enable/disable controls
- RF sweep trigger and results viewer
- Familiar device management

## Cross-Cutting Requirements

### Security
- No cloud services, no accounts, no telemetry, no analytics
- All data stored locally on the phone
- BLE connection must be bonded/encrypted
- WiFi connection to base station uses HTTPS or local-only HTTP
- Stealth mode: app can be renamed/icon-changed, no CYT branding visible
- Duress: configurable gesture to wipe all app data

### Privacy
- No network requests except to the user's own ESP32/Pi
- No location data sent anywhere
- No device fingerprints sent anywhere
- Camera/microphone permissions: never requested

### Accessibility
- WCAG AA contrast ratios
- Screen reader support for all alert text
- Haptic patterns for alert severity (distinct patterns per tier)
- Large text mode
- Simple mode: single screen showing "safe / something detected / review recommended"

### Alert Language (all tiers)
The app MUST use the same five-tier alert framework as the base station:

| Score Range | Tier | Language Style |
|------------|------|---------------|
| 0.0 - 0.3 | SILENT | Logged only, no notification |
| 0.3 - 0.5 | INFORMATIONAL | "A device was observed nearby." |
| 0.5 - 0.7 | NOTABLE | "A device has been observed at N of your locations." |
| 0.7 - 0.85 | ELEVATED | "A device has been observed at multiple locations over N hours." |
| 0.85 - 1.0 | REVIEW | "A device has shown a persistent pattern across N locations." |

Rules: No "WARNING". No "ALERT". No "you are being followed". No raw scores. Include "what to do" guidance at every tier.

### Safety Resources
Always accessible from main menu (not just on alerts):
- National DV Hotline: 1-800-799-7233
- NNEDV Safety Net: techsafety.org
- Crisis Text Line: Text HOME to 741741
- RAINN: 1-800-656-4673
- Configurable local resources

## Communication Protocol Summary

```
┌──────────┐     BLE GATT      ┌──────────────┐     WiFi/HTTP      ┌──────────────┐
│  Phone   │◄──────────────────►│  ESP32       │                    │  Pi Base     │
│  App     │  alerts, status,   │  Handheld    │  SD card / USB     │  Station     │
│          │  commands          │              │──────────────────►  │              │
│          │◄───────────────────────────────────────────────────────►│              │
│          │           WiFi REST API / WebSocket                    │              │
└──────────┘                    └──────────────┘                    └──────────────┘
```

### BLE GATT Protocol (Phone ↔ ESP32)
- Service: `0000ff10-0000-1000-8000-00805f9b34fb`
- Alert Stream (ff11): ESP32 → Phone, notify, JSON
- Status (ff12): ESP32 → Phone, read/notify, JSON
- Device List (ff13): ESP32 → Phone, read, JSON
- Command (ff14): Phone → ESP32, write, JSON

### HTTP API (Phone ↔ Base Station)
- `GET /api/status` — scanner states, device counts, session info
- `GET /api/alerts` — current active alerts
- `GET /api/devices?min_score=0.5` — filtered device list
- `GET /api/sessions` — session history
- `GET /api/session/{id}` — session detail with sightings
- `POST /api/command` — scanner control, config changes
- `WS /api/stream` — real-time alert + status WebSocket

### Data Formats
All JSON. Device appearances use the same schema across all three systems:
```json
{
  "device_id": "findmy:a1b2c3d4e5f6",
  "source_type": "ble",
  "timestamp": 1700000000,
  "rssi": -65,
  "lat": 33.4500,
  "lon": -112.0700,
  "metadata": {"tracker_type": "findmy"}
}
```

## Health & Wellness Features (Phone Only)

These features were originally on the handheld but moved to the phone per social psychology review. The phone provides OS-level encryption, biometric lock, and app-hiding — protection the ESP32 cannot match. If the handheld is discovered or seized, it contains only step data.

### Mood Tracker
- 1-10 scale mood logging with timestamp and optional GPS
- 7-day rolling average with trend detection (improving/stable/declining)
- 24-hour check-in reminders (configurable)
- Evidence-grade timestamped entries for legal documentation
- Export as CSV/PDF for therapy or legal proceedings
- **Privacy:** encrypted at rest by OS, NOT synced to handheld or base station
- **Correlation:** user can OPTIONALLY view mood entries alongside surveillance timeline, but the device never auto-correlates — a human analyst draws conclusions

### Cycle/Fertility Tracker
- Calendar-based menstrual cycle tracking
- Fertile window prediction (adjusted to individual cycle length)
- Basal body temperature logging
- **Privacy:** most sensitive data in the system. Never transmitted over BLE. Never appears in session exports. Encrypted at rest by OS. Consider separate PIN/biometric within the app.
- **Legal:** post-Dobbs implications require jurisdiction-aware data handling

### Sobriety Counter
- Days/hours since user-set date
- Milestone tracking (30 days, 90 days, 6 months, 1 year, etc.)
- **Privacy:** hidden by default (opt-in feature). Never visible without app unlock. If the user is in a recovery program, this data is protected health information.

### Design Principles for Health Features
- Health screens are architecturally separated from surveillance alerts — a surveillance notification NEVER intrudes on a health screen
- Health data is never transmitted to the ESP32 handheld
- Health data is never included in base station session uploads
- Duress wipe on the phone destroys health data (it's evidence the app does more than track BLE devices)
- Export is user-initiated only, in formats suitable for therapy, medical, or legal use

## Specialist Review Needed

The health features and the overall device concept (wellness tool with hidden surveillance detection) should be reviewed by a **trauma-informed design specialist** or **clinical psychologist with DV/forensic experience** — not a social psychologist. Key questions:
- How do trauma survivors (freeze/fawn/dissociation) interact with devices under duress?
- What do prosecutors and victim advocates actually need for evidence?
- How do abusers discover and weaponize technology in DV contexts?
- Safety planning methodology (professional discipline, not software feature)

Recommended resource: NNEDV Safety Net project (techsafety.org).

## Out of Scope (v1)

- Cloud sync between multiple phones
- Social/community features (group polarization risk per social psych review)
- Apple Watch native app (use standard iOS notification forwarding)
- Wear OS native app (use standard Android notification forwarding)
- Widget/live activity on lock screen (v2)
- Free-text journaling (too much discoverable data)
- Gamification of any health feature (inappropriate for this context)
- Sleep tracking (accelerometer data is too intimate if discovered)
