#pragma once
#include "types.h"

// ---- Wi-Fi ---------------------------------------------------------------
// The ESP32 only does 2.4 GHz Wi-Fi.
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASS "your-wifi-password"

// ---- TVs -----------------------------------------------------------------
static const TvConfig TV_CONFIG[] = {
  // name       ip                 client key (from the Python script)   MAC for Wake-on-LAN
  {"Lounge",  "192.168.8.248",   ""                                  ,   ""},
  {"Kitchen", "",                "",                                   ""},  // fill in the IP; it will pair itself
};

// ---- Taking over idle TVs ------------------------------------------------
// App IDs that mean "nobody is watching anything". Watch the serial monitor
// to see what each TV reports when it is on the home screen or screensaver,
// and add those IDs here.
static const char* IDLE_APPS[] = {
  "com.webos.app.home",
  "com.webos.app.screensaver",
};

#define IDLE_MINUTES 5           // how long a TV must be idle before taking over
#define TAKEOVER_START_HOUR 7    // only take over between these hours (local time)
#define TAKEOVER_END_HOUR 22

// ---- Scheduled wake-ups --------------------------------------------------
// Wakes a TV (needs its MAC above, and "Turn on via Wi-Fi" enabled on the TV)
// and shows the dashboard. Set tv to -1 to disable.
static const WakeTime WAKE_SCHEDULE[] = {
  {1, 17, 0},   // Kitchen TV at 17:00
};

// ---- Time ----------------------------------------------------------------
#define TZ_INFO "GMT0BST,M3.5.0/1,M10.5.0"   // UK time with daylight saving

// ---- Calendar --------------------------------------------------------------
// A private calendar's "Secret address in iCal format" (Google Calendar ->
// Settings -> this calendar, under "Integrate calendar"). Leave blank to
// disable. Treat this like a password: anyone with the URL can read the
// calendar, which is why it lives here and not in the repo.
#define CALENDAR_ICS_URL ""
#define CALENDAR_REFRESH_MINUTES 15

// ---- Locations --------------------------------------------------------------
// Supabase project URL and secret (service_role-style, "sb_secret_...") API
// key - Project Settings -> API in the Supabase dashboard. The secret key
// bypasses Row Level Security, so treat it like a password: it never leaves
// the ESP32 (the dashboard page reads the already-fetched copy, not Supabase
// directly). Leave the URL blank to disable.
#define SUPABASE_PROJECT_URL ""
#define SUPABASE_API_KEY ""
#define LOCATIONS_REFRESH_SECONDS 20
// Preferred: the TV browser reads locations straight from Supabase (the ESP32
// has too little memory to keep its own TLS connection to Supabase going).
// Create a "tv" user in Supabase, give it the read policy from
// supabase/schema.sql, and fill these in. The key is the *publishable* key.
// While any of them is blank the ESP32 fetches locations itself (fragile).
#define TV_EMAIL ""
#define TV_PASSWORD ""
#define SUPABASE_ANON_KEY ""
// Home, for the house icon on the TV map. 0 = no icon.
#define HOME_LAT 0.0
#define HOME_LNG 0.0
