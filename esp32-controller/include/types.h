#pragma once

struct TvConfig {
  const char* name;       // shown in logs
  const char* ip;         // TV's IP address; "" to disable this TV
  const char* clientKey;  // from a previous pairing; "" to pair (prompt appears on the TV)
  const char* mac;        // TV's MAC for Wake-on-LAN, e.g. "a8:23:fe:12:34:56"; "" if unknown
};

struct WakeTime {
  int tv;      // index into TV_CONFIG; -1 disables this entry
  int hour;    // 0-23, local time
  int minute;  // 0-59
};
