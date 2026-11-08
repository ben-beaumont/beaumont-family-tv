// Family Hub - ESP32 controller for LG webOS TVs
//
// - Keeps a connection to each TV (the TV's local control API, port 3001)
// - Watches which app is in front and whether the screen is on
// - When a TV has been idle (home screen / screensaver) for a while,
//   opens the dashboard page in the TV's web browser
// - Wakes a TV at scheduled times with Wake-on-LAN and shows the dashboard
// - Serves the stand-in dashboard and a status page itself
//
// Web endpoints on the ESP32:
//   /                 the dashboard page (today + upcoming calendar)
//   /status           JSON: what each TV is doing
//   /launch?tv=N      show the dashboard on TV N now
//   /wake?tv=N        wake TV N and show the dashboard
//   /home?tv=N        send TV N back to its own home screen (dashboard calls
//                     this itself on any remote keypress)
//   /heartbeat?tv=N   dashboard page pings this while it's actually loaded,
//                     so we can tell when it's gone even if the TV won't say
//   /calendar.ics     cached copy of the calendar feed, for the dashboard page
//   /calendar/refresh re-fetch the calendar feed now
//   /locations.json   cached copy of everyone's location, from Supabase
//   /locations/refresh re-fetch locations now

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "config.h"
#include "page.h"

// Google Trust Services' root CA (self-signed, valid to 2036), used to
// verify the HTTPS connection when fetching the calendar feed. Pinning one
// root keeps this lighter than embedding a full CA bundle.
static const char GOOGLE_ROOT_CA[] =
"-----BEGIN CERTIFICATE-----\n"
"MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD\n"
"VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG\n"
"A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw\n"
"WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz\n"
"IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi\n"
"AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi\n"
"QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR\n"
"HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW\n"
"BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D\n"
"9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8\n"
"p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD\n"
"-----END CERTIFICATE-----\n";

static const size_t CALENDAR_MAX_BYTES = 300000;  // guard against an unexpectedly huge feed
static String calendarIcs;
static uint32_t calendarFetchedAt = 0;  // millis() of last successful fetch; 0 = never

static void fetchCalendar() {
  if (!strlen(CALENDAR_ICS_URL)) return;
  WiFiClientSecure client;
  client.setCACert(GOOGLE_ROOT_CA);
  HTTPClient https;
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  https.setTimeout(15000);
  if (!https.begin(client, CALENDAR_ICS_URL)) {
    Serial.println("[calendar] couldn't start request");
    return;
  }
  int code = https.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[calendar] fetch failed: HTTP %d\n", code);
    https.end();
    return;
  }
  int len = https.getSize();
  if (len > (int)CALENDAR_MAX_BYTES) {
    Serial.printf("[calendar] feed too big (%d bytes) - skipped\n", len);
    https.end();
    return;
  }
  calendarIcs = https.getString();
  calendarFetchedAt = millis();
  Serial.printf("[calendar] fetched %u bytes\n", (unsigned)calendarIcs.length());
  https.end();
}

static const size_t LOCATIONS_MAX_BYTES = 50000;
static String locationsJson;
static uint32_t locationsFetchedAt = 0;  // millis() of last successful fetch; 0 = never

static void fetchLocations() {
  if (!strlen(SUPABASE_PROJECT_URL)) return;
  WiFiClientSecure client;
  client.setCACert(GOOGLE_ROOT_CA);  // Supabase's TLS chain also roots at GTS Root R4
  HTTPClient https;
  https.setTimeout(15000);
  String url = String(SUPABASE_PROJECT_URL) + "/rest/v1/locations?select=*";
  if (!https.begin(client, url)) {
    Serial.println("[locations] couldn't start request");
    return;
  }
  https.addHeader("apikey", SUPABASE_API_KEY);
  https.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  int code = https.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[locations] fetch failed: HTTP %d\n", code);
    https.end();
    return;
  }
  int len = https.getSize();
  if (len > (int)LOCATIONS_MAX_BYTES) {
    Serial.printf("[locations] response too big (%d bytes) - skipped\n", len);
    https.end();
    return;
  }
  locationsJson = https.getString();
  locationsFetchedAt = millis();
  Serial.printf("[locations] fetched %u bytes\n", (unsigned)locationsJson.length());
  https.end();
}

// Permissions requested when pairing. Same list as the Python script, so the
// client key it got from the lounge TV works here too.
static const char* PERMISSIONS[] = {
  "APP_TO_APP", "CLOSE", "CONTROL_AUDIO", "CONTROL_DISPLAY",
  "CONTROL_INPUT_JOYSTICK", "CONTROL_INPUT_MEDIA_PLAYBACK",
  "CONTROL_INPUT_MEDIA_RECORDING", "CONTROL_INPUT_TEXT", "CONTROL_INPUT_TV",
  "CONTROL_MOUSE_AND_KEYBOARD", "CONTROL_POWER", "CONTROL_TV_SCREEN", "LAUNCH",
  "LAUNCH_WEBAPP", "READ_APP_STATUS", "READ_COUNTRY_INFO", "READ_CURRENT_CHANNEL",
  "READ_INPUT_DEVICE_LIST", "READ_INSTALLED_APPS", "READ_LGE_SDX",
  "READ_LGE_TV_INPUT_EVENTS", "READ_NETWORK_STATE", "READ_NOTIFICATIONS",
  "READ_POWER_STATE", "READ_RUNNING_APPS", "READ_SETTINGS", "READ_TV_CHANNEL_LIST",
  "READ_TV_CURRENT_TIME", "READ_UPDATE_INFO", "SEARCH", "TEST_OPEN",
  "TEST_PROTECTED", "TEST_SECURE", "UPDATE_FROM_REMOTE_APP",
  "WRITE_NOTIFICATION_ALERT", "WRITE_NOTIFICATION_TOAST", "WRITE_SETTINGS",
};

static const uint32_t WAKE_LAUNCH_WINDOW_MS = 5UL * 60 * 1000;  // give up showing after a wake
static const uint32_t IDLE_MS = (uint32_t)IDLE_MINUTES * 60 * 1000;

// The TV's own screensaver has no "disable" API and overrides any foreground
// app, including our dashboard. While the dashboard is on screen we nudge its
// pointer input every so often so the TV sees "real" input and leaves its
// screensaver off; this is a known workaround, not an official feature.
static const char* DASHBOARD_APP_ID = "com.webos.app.browser";
static const uint32_t POINTER_KEEPALIVE_MS = 30UL * 1000;

// The "fg" subscription (below) doesn't always push an update for every
// transition - observed after exiting the browser via the remote's Back
// button, which left fgApp stuck reporting "browser" indefinitely. This
// actively re-asks every so often so that kind of drift self-corrects
// quickly instead of silently breaking idle detection.
static const uint32_t FG_POLL_MS = 5UL * 1000;

// Belt and braces for the same problem: webOS's own getForegroundAppInfo
// can itself keep reporting "browser" after a Back-button exit, not just
// its push subscription - re-asking doesn't help when the TV's answer is
// simply wrong. The dashboard page pings /heartbeat every 20s while it's
// genuinely loaded; if those stop arriving while fgApp still claims it's
// up, we stop trusting that claim regardless of what the TV says.
static const uint32_t HEARTBEAT_TIMEOUT_MS = 60UL * 1000;

struct Tv {
  const TvConfig* cfg = nullptr;
  int index = 0;
  bool enabled = false;
  WebSocketsClient ws;
  String key;
  bool connected = false;
  bool registered = false;
  String fgApp;            // foreground app id
  String power;            // "Active", "Screen Off", "Active Standby", ...
  uint32_t idleSince = 0;  // millis() when it went idle; 0 = not idle
  uint32_t launchDeadline = 0;  // after a scheduled wake: show the dashboard until this time
  uint32_t lastFgPoll = 0;  // last time we actively re-checked the foreground app
  uint32_t lastHeartbeat = 0;  // last /heartbeat ping received from the dashboard page

  // Second connection: the TV's pointer-input socket, used only for the
  // screensaver keep-alive. Opened lazily (only while this TV is actually
  // showing the dashboard) and closed again afterwards - with two TVs,
  // keeping it open for both all the time was enough concurrent TLS
  // sessions to exhaust the ESP32's heap and start failing handshakes.
  WebSocketsClient wsPtr;
  bool pointerRequested = false;
  bool pointerConnected = false;
  uint32_t lastKeepAlive = 0;
};

static constexpr size_t NUM_TVS = sizeof(TV_CONFIG) / sizeof(TV_CONFIG[0]);
static Tv tvs[NUM_TVS];
static WebServer server(80);
static Preferences prefs;

// Only one TV's pointer socket (screensaver keep-alive) is allowed open at
// a time, regardless of how many are showing the dashboard. Found live:
// two TVs each wanting their own pointer socket - 4 concurrent TLS
// sessions total with their main connections - was enough to exhaust the
// ESP32's heap and make the web server itself stop responding, not just
// the keep-alive. -1 = nobody currently holds it.
static int activePointerTv = -1;

#define LOG(tv, fmt, ...) Serial.printf("[%s] " fmt "\n", (tv).cfg->name, ##__VA_ARGS__)

// ------------------------------------------------------------------ helpers

static String dashboardUrl() {
  return "http://" + WiFi.localIP().toString() + "/";
}

static bool isIdleApp(const String& app) {
  for (const char* a : IDLE_APPS) {
    if (app == a) return true;
  }
  return false;
}

// Whether we have genuine recent confirmation (a heartbeat ping from the
// page itself) that the dashboard is actually loaded and running, rather
// than just taking the TV's word for it - see HEARTBEAT_TIMEOUT_MS.
static bool isDashboardAlive(const Tv& tv) {
  return tv.fgApp == DASHBOARD_APP_ID && millis() - tv.lastHeartbeat < HEARTBEAT_TIMEOUT_MS;
}

// A TV counts as idle because its foreground app is one of IDLE_APPS (home
// screen etc.), because webOS has already decided to show its own
// screensaver (power state "Screen Saver", not any particular foreground
// app), or because it's supposedly showing our dashboard but hasn't proven
// that with a heartbeat in a while - most likely the user navigated away
// and the TV just hasn't told us.
static bool isIdle(const Tv& tv) {
  return isIdleApp(tv.fgApp) || tv.power == "Screen Saver" ||
         (tv.fgApp == DASHBOARD_APP_ID && !isDashboardAlive(tv));
}

static bool withinTakeoverHours() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return true;  // no clock yet: don't block
  return t.tm_hour >= TAKEOVER_START_HOUR && t.tm_hour < TAKEOVER_END_HOUR;
}

static void sendDoc(Tv& tv, JsonDocument& doc) {
  String s;
  serializeJson(doc, s);
  tv.ws.sendTXT(s);
}

static void ssap(Tv& tv, const char* type, const char* id, const char* uri,
                 JsonObjectConst payload = JsonObjectConst()) {
  JsonDocument doc;
  doc["type"] = type;
  doc["id"] = id;
  doc["uri"] = uri;
  if (!payload.isNull()) doc["payload"] = payload;
  sendDoc(tv, doc);
}

static void saveKey(Tv& tv) {
  char k[8];
  snprintf(k, sizeof k, "k%d", tv.index);
  prefs.putString(k, tv.key);
}

static String loadKey(int index) {
  char k[8];
  snprintf(k, sizeof k, "k%d", index);
  return prefs.getString(k, "");
}

// ------------------------------------------------------------- TV actions

static void sendRegister(Tv& tv) {
  JsonDocument doc;
  doc["type"] = "register";
  doc["id"] = "register_0";
  JsonObject p = doc["payload"].to<JsonObject>();
  p["forcePairing"] = false;
  p["pairingType"] = "PROMPT";
  if (tv.key.length()) p["client-key"] = tv.key;
  JsonObject m = p["manifest"].to<JsonObject>();
  m["appVersion"] = "1.1";
  m["manifestVersion"] = 1;
  JsonArray perms = m["permissions"].to<JsonArray>();
  for (const char* perm : PERMISSIONS) perms.add(perm);
  sendDoc(tv, doc);
}

static void launchDashboard(Tv& tv) {
  String url = dashboardUrl() + "?tv=" + tv.index;
  JsonDocument pl;
  pl["target"] = url;
  ssap(tv, "request", "launch", "ssap://system.launcher/open", pl.as<JsonObjectConst>());
  // Optimistic starting point so isDashboardAlive() has a grace period for
  // the page to actually load and send its first real heartbeat.
  tv.lastHeartbeat = millis();
  LOG(tv, "showing dashboard %s", url.c_str());
}

// Sends the TV back to its own home screen - used when the dashboard page
// sees a remote keypress, so any button (other than power, which never
// reaches a web page) dismisses it like a screensaver would.
static void sendToHome(Tv& tv) {
  JsonDocument pl;
  pl["id"] = "com.webos.app.home";
  ssap(tv, "request", "home", "ssap://system.launcher/launch", pl.as<JsonObjectConst>());
  LOG(tv, "dismissed to home screen");
}

// Splits a "wss://host:port/path" URL as returned by getPointerInputSocket.
static bool parseWsUrl(const String& url, String& host, uint16_t& port, String& path) {
  int hostStart = url.indexOf("://");
  if (hostStart < 0) return false;
  hostStart += 3;
  int pathStart = url.indexOf('/', hostStart);
  String authority = pathStart < 0 ? url.substring(hostStart) : url.substring(hostStart, pathStart);
  path = pathStart < 0 ? "/" : url.substring(pathStart);
  int colon = authority.indexOf(':');
  if (colon < 0) {
    host = authority;
    port = 3001;
  } else {
    host = authority.substring(0, colon);
    port = (uint16_t)authority.substring(colon + 1).toInt();
  }
  return host.length() > 0;
}

static void connectPointerSocket(Tv& tv, const String& socketPath) {
  String host, path;
  uint16_t port;
  if (!parseWsUrl(socketPath, host, port, path)) {
    LOG(tv, "can't parse pointer socket path: %s", socketPath.c_str());
    return;
  }
  tv.wsPtr.beginSSL(host.c_str(), port, path.c_str(), "", "");
  LOG(tv, "pointer input socket ready");
}

static bool parseMac(const char* s, uint8_t mac[6]) {
  unsigned int b[6];
  if (sscanf(s, "%x%*c%x%*c%x%*c%x%*c%x%*c%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
    return false;
  for (int i = 0; i < 6; i++) mac[i] = (uint8_t)b[i];
  return true;
}

static void wakeOnLan(Tv& tv) {
  uint8_t mac[6];
  if (!parseMac(tv.cfg->mac, mac)) {
    LOG(tv, "can't wake: no MAC address set in config.h");
    return;
  }
  uint8_t pkt[102];
  memset(pkt, 0xFF, 6);
  for (int i = 0; i < 16; i++) memcpy(pkt + 6 + i * 6, mac, 6);
  WiFiUDP udp;
  for (int r = 0; r < 3; r++) {
    udp.beginPacket(WiFi.broadcastIP(), 9);
    udp.write(pkt, sizeof pkt);
    udp.endPacket();
    delay(50);
  }
  LOG(tv, "sent Wake-on-LAN");
}

static void wakeAndShow(Tv& tv) {
  if (!tv.enabled) return;
  tv.launchDeadline = millis() + WAKE_LAUNCH_WINDOW_MS;
  if (!(tv.registered && tv.power == "Active")) wakeOnLan(tv);
}

// ------------------------------------------------------- TV message handling

// Recomputes idleSince from the current foreground app + power state.
// Called whenever either one changes.
static void updateIdleState(Tv& tv) {
  bool idle = isIdle(tv);
  if (idle && !tv.idleSince) tv.idleSince = millis();
  if (!idle) tv.idleSince = 0;
}

static void setForeground(Tv& tv, const String& app) {
  if (app == tv.fgApp) return;
  tv.fgApp = app;
  updateIdleState(tv);
  LOG(tv, "foreground app: %s%s", app.length() ? app.c_str() : "(none)",
      tv.idleSince ? "  [idle]" : "");
}

static void onMessage(Tv& tv, const uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, (const char*)data, len)) {
    LOG(tv, "unreadable message from TV");
    return;
  }
  const char* type = doc["type"] | "";
  const char* id = doc["id"] | "";
  JsonObjectConst pl = doc["payload"].as<JsonObjectConst>();

  if (!strcmp(type, "registered")) {
    const char* k = pl["client-key"] | "";
    if (*k && tv.key != k) {
      tv.key = k;
      saveKey(tv);
      LOG(tv, "paired - client key saved: %s", k);
    }
    tv.registered = true;
    LOG(tv, "connected and authorised");
    JsonDocument sub;
    sub["subscribe"] = true;
    ssap(tv, "subscribe", "fg", "ssap://com.webos.applicationManager/getForegroundAppInfo");
    ssap(tv, "subscribe", "pwr", "ssap://com.webos.service.tvpower/power/getPowerState",
         sub.as<JsonObjectConst>());
    ssap(tv, "request", "netinfo", "ssap://com.webos.service.connectionmanager/getinfo");
    return;
  }

  if (!strcmp(type, "response") && !strcmp(id, "register_0")) {
    if (!strcmp(pl["pairingType"] | "", "PROMPT"))
      LOG(tv, ">>> accept the connection prompt on the TV with the remote <<<");
    return;
  }

  if (!strcmp(type, "error")) {
    LOG(tv, "TV error on '%s': %s", id, doc["error"] | "?");
    return;
  }

  if (!strcmp(id, "fg") || !strcmp(id, "fgpoll")) {
    setForeground(tv, String(pl["appId"] | ""));
  } else if (!strcmp(id, "pwr")) {
    String state = pl["state"] | "";
    if (state.length() && state != tv.power) {
      tv.power = state;
      LOG(tv, "power: %s", state.c_str());
      updateIdleState(tv);
    }
  } else if (!strcmp(id, "netinfo")) {
    // Printed once so you can copy the TV's MAC address into config.h
    Serial.printf("[%s] network info: ", tv.cfg->name);
    serializeJson(pl, Serial);
    Serial.println();
  } else if (!strcmp(id, "ptr")) {
    const char* socketPath = pl["socketPath"] | "";
    if (*socketPath) connectPointerSocket(tv, String(socketPath));
  } else if (!strcmp(id, "launch")) {
    if (!(pl["returnValue"] | false)) LOG(tv, "TV refused to open the dashboard");
  }
}

static void onWsEvent(Tv& tv, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      tv.connected = true;
      LOG(tv, "socket open, registering...");
      sendRegister(tv);
      break;
    case WStype_DISCONNECTED:
      if (tv.connected) LOG(tv, "disconnected (TV off or unreachable)");
      tv.connected = tv.registered = false;
      tv.fgApp = "";
      tv.power = "";
      tv.idleSince = 0;
      break;
    case WStype_TEXT:
      onMessage(tv, payload, length);
      break;
    default:
      break;
  }
}

// --------------------------------------------------------------- the rules

static void checkTv(Tv& tv) {
  if (!tv.registered) return;
  // "Screen Saver" means webOS's own screensaver is already showing - the
  // panel is lit, just not on anything useful, so it still counts as a
  // screen we can take over (and should: launching our dashboard is what
  // dismisses it).
  bool screenOn = tv.power == "Active" || tv.power == "Screen Saver";
  uint32_t now = millis();

  if (now - tv.lastFgPoll >= FG_POLL_MS) {
    tv.lastFgPoll = now;
    ssap(tv, "request", "fgpoll", "ssap://com.webos.applicationManager/getForegroundAppInfo");
  }

  // Re-evaluate every tick, not just on fg/power change events, so a
  // heartbeat timing out (a time-based condition) starts the idle clock
  // even without any new message from the TV.
  updateIdleState(tv);

  // While our own dashboard is on screen, nudge the TV's pointer input so it
  // sees "real" activity and doesn't let its own screensaver take over.
  // The pointer socket is only opened for as long as that's actually true,
  // and only for one TV at a time - see activePointerTv above.
  bool holdsPointerSlot = activePointerTv == -1 || activePointerTv == tv.index;
  if (isDashboardAlive(tv) && holdsPointerSlot) {
    if (!tv.pointerConnected && !tv.pointerRequested) {
      tv.pointerRequested = true;
      activePointerTv = tv.index;
      ssap(tv, "request", "ptr", "ssap://com.webos.service.networkinput/getPointerInputSocket");
    }
    if (tv.pointerConnected && now - tv.lastKeepAlive >= POINTER_KEEPALIVE_MS) {
      tv.wsPtr.sendTXT("type:move\ndx:1\ndy:0\ndown:0\n\n");
      tv.wsPtr.sendTXT("type:move\ndx:-1\ndy:0\ndown:0\n\n");
      tv.lastKeepAlive = now;
      LOG(tv, "pointer keep-alive sent");
    }
  } else if (tv.pointerConnected || tv.pointerRequested) {
    tv.wsPtr.disconnect();
    tv.pointerConnected = false;
    tv.pointerRequested = false;
    if (activePointerTv == tv.index) activePointerTv = -1;
    LOG(tv, "pointer socket closed");
  }

  // After a scheduled wake: show the dashboard as soon as the TV is up.
  if (tv.launchDeadline) {
    if ((int32_t)(now - tv.launchDeadline) > 0) {
      tv.launchDeadline = 0;
    } else if (screenOn && tv.fgApp.length()) {
      tv.launchDeadline = 0;
      launchDashboard(tv);
      return;
    }
  }

  // Idle long enough on the home screen / screensaver: take over.
  if (screenOn && tv.idleSince && now - tv.idleSince >= IDLE_MS && withinTakeoverHours()) {
    launchDashboard(tv);
    tv.idleSince = now;  // if it didn't work, try again after another idle period
  }
}

static void checkSchedule() {
  static int lastMinute = -1;
  struct tm t;
  if (!getLocalTime(&t, 0) || t.tm_min == lastMinute) return;
  lastMinute = t.tm_min;
  for (const WakeTime& w : WAKE_SCHEDULE) {
    if (w.tv < 0 || w.tv >= (int)NUM_TVS) continue;
    if (w.hour == t.tm_hour && w.minute == t.tm_min) {
      LOG(tvs[w.tv], "scheduled wake");
      wakeAndShow(tvs[w.tv]);
    }
  }
}

// ---------------------------------------------------------------- web pages

static Tv* tvFromArg() {
  int i = server.arg("tv").toInt();
  if (i < 0 || i >= (int)NUM_TVS || !tvs[i].enabled) return nullptr;
  return &tvs[i];
}

static void setupWeb() {
  // send() would copy PAGE_HTML into a heap String first; under memory
  // pressure that large allocation can silently fail, sending a 200 with
  // an empty body (a blank page on the TV) instead of an error. send_P
  // writes straight from flash, no big heap copy needed.
  server.on("/", []() { server.send_P(200, "text/html", PAGE_HTML); });

  server.on("/status", []() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (Tv& tv : tvs) {
      JsonObject o = arr.add<JsonObject>();
      o["name"] = tv.cfg->name;
      o["ip"] = tv.cfg->ip;
      o["enabled"] = tv.enabled;
      o["connected"] = tv.registered;
      o["power"] = tv.power;
      o["foreground"] = tv.fgApp;
      o["idleSeconds"] = tv.idleSince ? (millis() - tv.idleSince) / 1000 : 0;
      if (tv.fgApp == DASHBOARD_APP_ID) o["dashboardAlive"] = isDashboardAlive(tv);
    }
    String s;
    serializeJsonPretty(doc, s);
    server.send(200, "application/json", s);
  });

  server.on("/launch", []() {
    Tv* tv = tvFromArg();
    if (!tv || !tv->registered) return server.send(409, "text/plain", "TV not connected");
    launchDashboard(*tv);
    server.send(200, "text/plain", "ok");
  });

  server.on("/wake", []() {
    Tv* tv = tvFromArg();
    if (!tv) return server.send(404, "text/plain", "no such TV");
    wakeAndShow(*tv);
    server.send(200, "text/plain", "ok");
  });

  server.on("/home", []() {
    Tv* tv = tvFromArg();
    if (!tv || !tv->registered) return server.send(409, "text/plain", "TV not connected");
    sendToHome(*tv);
    server.send(200, "text/plain", "ok");
  });

  // Pinged by the dashboard page itself while it's actually loaded - see
  // isDashboardAlive(). No-op if the tv arg doesn't resolve; the page
  // can't do anything useful with an error here anyway.
  server.on("/heartbeat", []() {
    Tv* tv = tvFromArg();
    if (tv) tv->lastHeartbeat = millis();
    server.send(200, "text/plain", "ok");
  });

  server.on("/calendar.ics", []() {
    if (!calendarFetchedAt) return server.send(503, "text/plain", "calendar not fetched yet");
    server.send(200, "text/calendar", calendarIcs);
  });

  server.on("/calendar/refresh", []() {
    fetchCalendar();
    server.send(200, "text/plain", calendarFetchedAt ? "ok" : "fetch failed - see serial log");
  });

  server.on("/locations.json", []() {
    if (!locationsFetchedAt) return server.send(503, "text/plain", "locations not fetched yet");
    server.send(200, "application/json", locationsJson);
  });

  server.on("/locations/refresh", []() {
    fetchLocations();
    server.send(200, "text/plain", locationsFetchedAt ? "ok" : "fetch failed - see serial log");
  });

  server.begin();
}

// ------------------------------------------------------------- setup / loop

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nFamily Hub controller starting");

  prefs.begin("tvs", false);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("family-hub");
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to Wi-Fi '%s'", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.printf("\nWi-Fi connected. ESP32 address: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("Dashboard: %s   Status: %sstatus\n", dashboardUrl().c_str(), dashboardUrl().c_str());

  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
  setupWeb();
  fetchCalendar();
  fetchLocations();

  for (size_t i = 0; i < NUM_TVS; i++) {
    Tv& tv = tvs[i];
    tv.cfg = &TV_CONFIG[i];
    tv.index = i;
    tv.enabled = tv.cfg->ip && *tv.cfg->ip;
    if (!tv.enabled) {
      LOG(tv, "no IP set - skipped");
      continue;
    }
    tv.key = loadKey(i);
    if (!tv.key.length()) tv.key = tv.cfg->clientKey;
    tv.ws.onEvent([&tv](WStype_t type, uint8_t* payload, size_t length) {
      onWsEvent(tv, type, payload, length);
    });
    tv.ws.setReconnectInterval(10000);
    tv.ws.enableHeartbeat(30000, 10000, 3);

    tv.wsPtr.onEvent([&tv](WStype_t type, uint8_t* payload, size_t length) {
      if (type == WStype_CONNECTED) {
        tv.pointerConnected = true;
        LOG(tv, "pointer socket connected");
      } else if (type == WStype_DISCONNECTED) {
        tv.pointerConnected = false;
      }
    });
    tv.wsPtr.setReconnectInterval(10000);
    // The TV uses a self-signed certificate on the local network: with no
    // fingerprint the library connects without verifying it. The empty
    // protocol stops it sending a WebSocket sub-protocol the TV doesn't know.
    tv.ws.beginSSL(tv.cfg->ip, 3001, "/", "", "");
    LOG(tv, "connecting to %s%s", tv.cfg->ip, tv.key.length() ? "" : " (will ask to pair)");
  }
}

void loop() {
  server.handleClient();
  for (Tv& tv : tvs) {
    if (tv.enabled) {
      tv.ws.loop();
      tv.wsPtr.loop();
    }
  }

  static uint32_t lastCheck = 0;
  if (millis() - lastCheck >= 1000) {
    lastCheck = millis();
    for (Tv& tv : tvs) {
      if (tv.enabled) checkTv(tv);
    }
    checkSchedule();
  }

  static uint32_t lastCalendarFetch = 0;
  uint32_t calendarIntervalMs = (uint32_t)CALENDAR_REFRESH_MINUTES * 60 * 1000;
  if (strlen(CALENDAR_ICS_URL) && millis() - lastCalendarFetch >= calendarIntervalMs) {
    lastCalendarFetch = millis();
    fetchCalendar();
  }

  static uint32_t lastLocationsFetch = 0;
  uint32_t locationsIntervalMs = (uint32_t)LOCATIONS_REFRESH_MINUTES * 60 * 1000;
  if (strlen(SUPABASE_PROJECT_URL) && millis() - lastLocationsFetch >= locationsIntervalMs) {
    lastLocationsFetch = millis();
    fetchLocations();
  }
}
