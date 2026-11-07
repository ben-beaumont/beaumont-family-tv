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
