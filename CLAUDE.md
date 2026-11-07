# Beaumont Family TV - project notes for Claude

## Goal
A family organiser on the two LG OLED TVs at home (lounge and kitchen):
- **Where everyone is**, shown on a map, plus **ETA home** so dinner can be
  timed. Ben wants to keep the map view, not just zone names.
- **Family calendars** (everyone uses Google accounts; everyone has an iPhone).
- Behaves like a **screensaver**: appears when a TV is idle, and at set times
  (e.g. the kitchen TV at dinner time).

## Agreed architecture
- **iPhone app** (Ben has an Apple developer account; distribute via TestFlight)
  - Background location via geofences + significant-location-change.
  - Computes its own ETA home with MapKit.
  - Everyone opts in and grants "Always" location; show zones by default
    rather than a live dot where possible.
- **Supabase** (hosted) as the data store: phones post location/zone/ETA, TVs
  read it. Chosen so phones never need to reach into the home network. Use
  per-person sign-in and row-level security - this is family location data.
- **TV app**: webOS web app (HTML/JS) sideloaded with LG Developer Mode
  (`@webos-tools/cli`, `ares-*` commands). Map via **MapKit JS** (dark scheme).
  Until it exists, the ESP32 serves a stand-in page that the TV browser opens.
- **ESP32 controller** (`esp32-controller/`): watches both TVs, takes over when
  idle, wakes the kitchen TV on a schedule (Wake-on-LAN), later keeps Developer
  Mode sessions renewed. A webOS app can't replace the real screensaver, so
  this "take over when idle" approach stands in for it.

### OLED rules (both TVs are OLED)
Dark backgrounds, dim text, nothing static for long: rotate views (map ->
calendar -> who's-home strip), drift the layout a few pixels every minute,
go dark at night.

## Current status
- `tools/lg_current_app.py` works (Python + `aiowebostv`): found the lounge TV
  and paired. Lounge TV: **192.168.8.248**, name "Lounge TV". Its client key is
  in `esp32-controller/include/config.h` (gitignored). Kitchen TV IP not known yet.
- `esp32-controller/` is written but **not yet compiled or tested** on hardware.
  Points to verify first:
  - Builds under PlatformIO (`esp32dev`, Arduino, WebSockets 2.6.x, ArduinoJson 7).
  - TLS WebSocket to the TV on port 3001 (self-signed cert; `beginSSL` with an
    empty fingerprint -> library calls `setInsecure()`).
  - Pairing reuses the lounge key (same permissions list as aiowebostv).
  - Which app IDs each TV reports when idle (home screen / screensaver) ->
    update `IDLE_APPS` in config.h from the serial log.
  - `ssap://system.launcher/open` with a `target` URL opens the TV browser.
  - `connectionmanager/getinfo` output, to get each TV's MAC for Wake-on-LAN.
- A Pine A64 was tried as the controller and abandoned (power/boot problems).

## Hardware and environment
- ESP32: classic **ESP32-D0WD-V3**, 4 MB flash, CP210x USB-serial on **COM3**.
- Ben's laptop: Windows, PowerShell, Python via `py`, PlatformIO in VS Code.
- Home network is 192.168.8.x.

## Next steps
1. Fill in Wi-Fi details in `config.h`, build and flash the ESP32, get both TVs
   connected, confirm idle detection and the launch of the stand-in page.
2. Build the webOS TV app with sample data (map + calendar + rotating views).
3. Set up Supabase schema and auth.
4. iPhone app: location, zones, ETA.
5. Calendars (Google Calendar API, or per-calendar private iCal links).

## Conventions
- Never commit `config.h` (Wi-Fi password, TV client keys).
- Keep family members' personal data out of the repo.
