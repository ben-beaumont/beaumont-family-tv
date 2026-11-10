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
- `esp32-controller/` builds and runs on hardware. Flashed to the ESP32 on
  COM3; connects to Wi-Fi, pairs with the lounge TV (reusing the key from
  the Python script), reports its foreground app and power state, and
  `/launch?tv=0` successfully opens the stand-in dashboard in the TV's
  browser (confirmed both in the serial log and on screen).
- webOS has no API to turn off its own screensaver, and it overrides any
  foreground app (including our dashboard) after a few minutes with no real
  remote input. Worked around by opening a second WebSocket to the TV's
  pointer-input socket and nudging it every 30s while the dashboard is in
  front, mimicking real input. You'll see a brief, tiny pointer flicker on
  screen every 30s; that's expected. Verified on hardware: the lounge TV no
  longer drops into its screensaver while the dashboard is up.
- Still to do on the ESP32 side:
  - Kitchen TV: get its IP, let it pair (accept the prompt on the TV), copy
    its MAC into `config.h` for Wake-on-LAN.
  - Confirm the idle-detection app IDs: watch the serial log for the
    `foreground app:` lines while using each TV normally, and add any app
    ID shown when idle (home screen / screensaver) to `IDLE_APPS` if it
    differs from the current guesses.
  - Let a TV sit idle for `IDLE_MINUTES` to confirm takeover actually
    triggers (so far only verified via manual `/launch`).
  - Test scheduled Wake-on-LAN (`/wake?tv=1`) once the kitchen TV's MAC is known.
- `tv-app/prototype/index.html` is a single-file design prototype for the
  webOS TV app, on sample data only (invented names, town and calendar). It
  shows the target layout: map on the left, a "start cooking at" dinner call
  worked out from the last ETA home, a who's-where list, and a three-day
  calendar strip. It has Kitchen and Lounge views, locks to the viewport with
  no scrolling on a 16:9 screen (checked at 1920x1080), and drifts a couple
  of pixels each minute. The map is a hand-drawn SVG stand-in for MapKit JS,
  and it is not wired to Supabase or the calendar feed yet.
- Live page updates (written, **not yet compiled or tested on hardware**): the
  dashboard's source of truth is now `esp32-controller/page/index.html`. The
  ESP32 serves a loader at `/`; the TV's browser fetches the page from
  `PAGE_URL` (default: raw GitHub, `master`), caches it in localStorage, and
  falls back to `/builtin` (the copy baked into the firmware). The fetch is in
  the browser on purpose - another TLS connection on the ESP32 would compete
  for heap with the TV sockets. `include/page.h` is generated from
  `page/index.html` by `tools/embed_page.py` before each build; never edit
  `page.h` directly. So: page changes = push to `master`; firmware changes
  (`src/`) = reflash.
- A Pine A64 was tried as the controller and abandoned (power/boot problems).

## Hardware and environment
- ESP32: classic **ESP32-D0WD-V3**, 4 MB flash, CP210x USB-serial on **COM3**.
- Ben's laptop: Windows, PowerShell, Python via `py`, PlatformIO in VS Code.
- Home network is 192.168.8.x.

## Next steps
1. Get the kitchen TV connected (IP, pairing, MAC) and confirm idle detection
   actually triggers a takeover on both TVs, not just manual `/launch`.
2. Build the webOS TV app with sample data (map + calendar + rotating views).
   Start from `tv-app/prototype/index.html` for layout and the dinner-timing
   logic; the OLED view rotation and night dimming are still to add.
3. Set up Supabase schema and auth.
4. iPhone app: location, zones, ETA.
5. Calendars (Google Calendar API, or per-calendar private iCal links).

## Conventions
- Never commit `config.h` (Wi-Fi password, TV client keys).
- Keep family members' personal data out of the repo.
