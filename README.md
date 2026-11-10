# Beaumont Family TV

A family organiser shown on the two LG OLED TVs at home (lounge and kitchen):
where everyone is, when they'll be home, and the family calendars.

| Folder | What it is |
|---|---|
| `esp32-controller/` | ESP32 firmware (PlatformIO) that watches both TVs and shows the dashboard when they're idle or at set times |
| `tv-app/prototype/` | Single-file HTML design prototype of the TV dashboard (map, dinner timing, calendar) running on sample data |
| `tools/lg_current_app.py` | Finds an LG TV on the network and shows the foreground app and installed apps |

See `CLAUDE.md` for the full plan and current status.
