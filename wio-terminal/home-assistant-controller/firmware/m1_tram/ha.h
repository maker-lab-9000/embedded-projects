// Home Assistant websocket session: connect, auth, subscribe, history refresh,
// keepalive ping, reconnect with backoff. A rejected token stops all retries
// until reset (HA bans IPs after repeated failed logins).
#pragma once
#include "secrets.h"
#include "ws_client.h"
#include "ha_proto.h"
#include "net.h"
#include "wdt.h"

#define HA_HIST_EVERY_MS  600000UL
#define HA_PING_EVERY_MS  30000UL
#define HA_PING_TIMEOUT_MS 10000UL
#define HA_AUTH_TIMEOUT_MS 10000UL
#define HA_HL_TIMEOUT_MS 10000UL  // a homelab history piece that gets no answer (e.g. too large) is skipped

enum HaStatus : uint8_t { HA_OFFLINE, HA_CONNECTING, HA_CONNECTED, HA_AUTH_REJECTED };

static WsClient haWs;
static HaProto haP;
static Backoff haBackoff;
static TramState* haState = nullptr;
static ClimateState* haClimate = nullptr;
static LightsState* haLights = nullptr;
static HeatingState* haHeating = nullptr;
static PowerState* haPower = nullptr;
static HomelabState* haHomelab = nullptr;
static GamingState* haGaming = nullptr;
static bool haHlFillWanted = false, haGpFillWanted = false;
static uint32_t haHlSent = 0, haGpSent = 0;
static HaStatus haSt = HA_OFFLINE;
static uint32_t haNextTry = 0, haConnStart = 0, haHistDue = 0, haPingDue = 0, haPingSent = 0;
static uint32_t haNextId = 2;

inline HaStatus haStatus() { return haSt; }
inline void haInit(TramState* s, ClimateState* c, LightsState* l, HeatingState* h, PowerState* pw, HomelabState* hl, GamingState* gp) {
  haState = s; haClimate = c; haLights = l; haHeating = h; haPower = pw; haHomelab = hl; haGaming = gp;
}

// Sends a light service call; false when not connected (nothing is queued).
inline bool haCallLight(LightAction a) {
  if (haSt != HA_CONNECTED) return false;
  char req[256];
  if (!lightBuildCall(req, sizeof(req), haNextId++, a)) return false;
  return haWs.sendText(req);
}

inline bool haCallHeating(HeatAction a) {
  if (haSt != HA_CONNECTED) return false;
  char req[256];
  if (!heatBuildCall(req, sizeof(req), haNextId++, a)) return false;
  return haWs.sendText(req);
}

inline void haDrop(const char* why) {
  haWs.close();
  haSt = HA_OFFLINE;
  haP = HaProto();
  uint32_t d = haBackoff.next();
  haNextTry = millis() + d;
  Serial.print("ha: offline ("); Serial.print(why); Serial.print("), retry in s ");
  Serial.println(d / 1000);
}

// Serial command 'r' in the sketch calls this to test reconnects.
inline void haForceReconnect() { if (haSt == HA_CONNECTED || haSt == HA_CONNECTING) haDrop("forced"); }

inline void haSendHistory() {
  uint32_t now = nowUtc();
  if (!now) return;  // need a clock for start_time; retried on the next poll
  char start[24], req[256];
  formatIsoUtc(now - 86400, start, sizeof(start));
  uint32_t id = haNextId++;
  snprintf(req, sizeof(req),
           "{\"id\":%lu,\"type\":\"history/history_during_period\",\"start_time\":\"%s\","
           "\"entity_ids\":[\"" TRAM_ENTITY_GAP "\"],\"minimal_response\":true,\"no_attributes\":true}",
           (unsigned long)id, start);
  if (haWs.sendText(req)) haP.histPending = id;
  haHistDue = millis() + HA_HIST_EVERY_MS;
}

inline void haHandle() {
  static JsonDocument filter;
  if (filter.isNull()) haBuildFilter(filter);
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, haWs.msg(), haWs.msgLen(), DeserializationOption::Filter(filter));
  if (err) { Serial.print("ha: json "); Serial.println(err.c_str()); return; }
  switch (haHandleMessage(haP, *haState, *haClimate, *haLights, *haHeating, *haPower, *haHomelab, *haGaming, doc.as<JsonVariantConst>())) {
    case HA_ACT_SEND_AUTH:
      haWs.sendText("{\"type\":\"auth\",\"access_token\":\"" HA_TOKEN "\"}");
      break;
    case HA_ACT_SUBSCRIBE: {
      static char sub[2400];
      if (haBuildSubscribe(sub, sizeof(sub))) haWs.sendText(sub);
      else Serial.println("ha: subscribe request too long");
      haSt = HA_CONNECTED;
      haBackoff.reset();
      haHistDue = millis();
      haHlFillWanted = haGpFillWanted = true;  // reload the graphs' history: there may be a gap
      haPingDue = millis() + HA_PING_EVERY_MS;
      Serial.println("ha: connected");
      break;
    }
    case HA_ACT_REJECTED:
      haWs.close();
      haSt = HA_AUTH_REJECTED;
      Serial.println("ha: TOKEN REJECTED - not retrying until reset");
      break;
    case HA_ACT_NONE:
      break;
  }
}

// Homelab temperature history: one piece at a time, after each connect.
inline void haPollHomelab(uint32_t ms) {
  if (haHlFillWanted && nowUtc()) { hlBeginFill(*haHomelab, nowUtc()); haHlFillWanted = false; }
  if (haP.hlPending) {
    if (ms - haHlSent < HA_HL_TIMEOUT_MS) return;
    Serial.println("ha: homelab history piece timed out, skipped");
    haP.hlPending = 0;
    hlFillAdvance(*haHomelab);
  }
  char req[256];
  uint32_t id = haNextId;
  if (!hlBuildRequest(req, sizeof(req), id, *haHomelab)) return;
  haNextId++;
  if (haWs.sendText(req)) { haP.hlPending = id; haHlSent = ms; }
}

// Gaming PC graphs: each sensor's last 15 min, one request at a time, after each connect.
inline void haPollGaming(uint32_t ms) {
  if (haGpFillWanted && nowUtc()) { gpBeginFill(*haGaming, nowUtc()); haGpFillWanted = false; }
  if (haP.gpPending) {
    if (ms - haGpSent < HA_HL_TIMEOUT_MS) return;
    Serial.println("ha: gaming history timed out, skipped");
    haP.gpPending = 0;
    gpFillAdvance(*haGaming);
  }
  char req[320];
  uint32_t id = haNextId;
  if (!gpBuildRequest(req, sizeof(req), id, *haGaming)) return;
  haNextId++;
  if (haWs.sendText(req)) { haP.gpPending = id; haGpSent = ms; }
}

inline void haPoll() {
  if (!haState || !haClimate || !haLights || !haHeating || !haPower || !haHomelab || !haGaming || haSt == HA_AUTH_REJECTED) return;
  uint32_t ms = millis();
  if (haSt == HA_OFFLINE) {
    if (!netUp() || (int32_t)(ms - haNextTry) < 0) return;
    haSt = HA_CONNECTING;
    haConnStart = ms;
    Serial.println("ha: connecting");
    wdtFeed();  // the connect can hang when HA is down; the watchdog then resets the board
    wdtCrumb(20);
    bool ok = haWs.connect(HA_HOST, HA_PORT, "/api/websocket");
    wdtCrumb(21);
    wdtFeed();
    if (!ok) { haDrop("connect failed"); return; }
    return;
  }
  wdtCrumb(22);
  if (!haWs.connected()) { haDrop("socket closed"); return; }
  wdtCrumb(23);
  for (int i = 0; i < 4 && haWs.poll(); i++) {  // a few messages per loop at most
    wdtCrumb(24);
    haHandle();
    wdtCrumb(25);
    if (haSt == HA_AUTH_REJECTED) return;
  }
  if (haSt == HA_CONNECTING) {
    if (ms - haConnStart > HA_AUTH_TIMEOUT_MS) haDrop("auth timeout");
    return;
  }
  wdtCrumb(26);
  if (!haP.histPending && (int32_t)(ms - haHistDue) >= 0) haSendHistory();
  wdtCrumb(27);
  haPollHomelab(ms);
  haPollGaming(ms);
  if (haP.pingPending) {
    if (ms - haPingSent > HA_PING_TIMEOUT_MS) haDrop("ping timeout");
  } else if ((int32_t)(ms - haPingDue) >= 0) {
    char req[40];
    uint32_t id = haNextId++;
    snprintf(req, sizeof(req), "{\"id\":%lu,\"type\":\"ping\"}", (unsigned long)id);
    if (haWs.sendText(req)) { haP.pingPending = id; haPingSent = ms; }
    haPingDue = ms + HA_PING_EVERY_MS;
  }
}
