# Family Hub - ESP32 TV controller

Watches both LG TVs. When one sits idle on the home screen or screensaver,
it opens the family dashboard in the TV's web browser. It can also wake a TV
at a set time (e.g. the kitchen TV at 17:00) and show the dashboard.

Until the real family app exists, the "dashboard" is a dark, slowly drifting
clock served by the ESP32 itself, which is safe for OLED screens.

## Set up

1. Install **VS Code**, then the **PlatformIO IDE** extension.
2. In VS Code: File > Open Folder > this `esp32-controller` folder.
   PlatformIO downloads the ESP32 tools and libraries the first time (a few minutes).
3. Edit `include/config.h` (kept out of git because it holds your Wi-Fi
   password and TV keys; `config.example.h` is the shareable template - copy
   it to `config.h` on a fresh checkout):
   - `WIFI_SSID` / `WIFI_PASS` - your Wi-Fi (2.4 GHz).
   - Kitchen TV IP address (TV: Settings > General > Network > your connection > Advanced).
     The lounge TV is already filled in, with the key from the Python script.
4. Plug the ESP32 in (it's on COM3), then click the **Upload** arrow in the
   PlatformIO toolbar at the bottom. If upload can't connect, hold the BOOT
   button on the board when "Connecting..." appears.
5. Click the **Serial Monitor** plug icon to watch what it does.

## First run

- The monitor prints the ESP32's address, e.g. `Dashboard: http://192.168.8.60/`.
  Open `http://<that address>/status` on your laptop to see both TVs' state.
- The kitchen TV shows a pairing prompt: accept it with the remote. The key is
  saved on the ESP32, so it only happens once.
- Watch the `foreground app:` lines while you use each TV. Lines marked
  `[idle]` are the ones it will take over. If the home screen or screensaver
  shows up with a different app ID, add it to `IDLE_APPS` in `config.h`.
- To test straight away: open `http://<esp32 address>/launch?tv=0` (lounge)
  or `?tv=1` (kitchen).

## Waking a TV on a schedule

1. On each TV, turn on **Turn on via Wi-Fi** (also called *Mobile TV On*,
   under Settings > General > Devices or Network, depending on model).
2. Copy the TV's MAC address into `config.h`. After pairing, the monitor
   prints the TV's network info once, which includes it; it's also in the
   TV's network settings.
3. Set the times in `WAKE_SCHEDULE`. Test with `http://<esp32 address>/wake?tv=1`
   while the TV is off.

## Changing the dashboard without reflashing

The dashboard page lives in `page/index.html`. The ESP32 serves a small loader
at `/`; the TV's browser uses it to fetch the latest `page/index.html` from
the repo's `master` branch on GitHub and show it. So to change what the TVs
show: edit `page/index.html`, commit, push to `master`.

- A TV already showing the dashboard checks every 10 minutes and reloads
  itself when the file has changed. GitHub caches raw files for about 5
  minutes, so allow up to 15 minutes, or re-open the page
  (`/launch?tv=0`) to pick it up sooner.
- The browser keeps the last good copy, so the page still shows if GitHub is
  slow or down. With nothing cached yet it falls back to the copy baked into
  the firmware, which you can always see at `/builtin`.
- `include/page.h` is that baked-in copy. It is generated from
  `page/index.html` by `tools/embed_page.py` on every build - don't edit it
  by hand. Reflash now and then so the fallback doesn't get too stale.
- To try a change on the TVs before merging, set `PAGE_URL` in `config.h` to
  the raw URL of your branch and reflash once. Set it to `""` to turn live
  updates off.
- The firmware itself (anything in `src/`) still needs flashing.

Because the page can read the family's locations, whoever can push to the
branch in `PAGE_URL` controls what runs on the TVs. Keep two-factor
authentication on the GitHub account.

## Calendar

The dashboard page rotates between the clock and an upcoming-events agenda
built from one calendar feed, every 20 seconds.

1. In Google Calendar: Settings -> pick the calendar -> "Integrate calendar"
   -> copy "Secret address in iCal format".
2. Paste it into `CALENDAR_ICS_URL` in `config.h`. Treat it like a password -
   anyone with that URL can read the calendar.
3. The ESP32 re-fetches it every `CALENDAR_REFRESH_MINUTES` (default 15); the
   TV page re-reads the ESP32's cached copy every 5 minutes. To force a fetch
   without waiting, open `http://<esp32 address>/calendar/refresh`.

Known limits of the built-in parser: recurring events only handle plain
daily/weekly/monthly/yearly rules and simple weekly `BYDAY` lists (no "2nd
Tuesday of the month" style rules), and event times are read as local time
unless they're explicitly UTC.

## Settings in config.h

| Setting | Meaning |
|---|---|
| `IDLE_APPS` | App IDs that count as "nobody's watching" |
| `IDLE_MINUTES` | How long idle before taking over (default 5) |
| `TAKEOVER_START_HOUR` / `END_HOUR` | Only take over between these hours |
| `WAKE_SCHEDULE` | Which TV to wake, and when |
| `CALENDAR_ICS_URL` | Secret iCal feed URL for the agenda view |
| `CALENDAR_REFRESH_MINUTES` | How often the ESP32 re-fetches the calendar |
