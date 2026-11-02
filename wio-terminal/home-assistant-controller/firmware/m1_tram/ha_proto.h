// Home Assistant websocket protocol handling without any I/O, so it can be
// host-tested (test/run.sh). ha.h does the sending and the timing.
#pragma once
#include <ArduinoJson.h>
#include <string.h>
#include "tram_model.h"
#include "climate_model.h"
#include "lights_model.h"
#include "heating_model.h"
#include "power_model.h"
#include "timeutil.h"

enum HaAction : uint8_t { HA_ACT_NONE, HA_ACT_SEND_AUTH, HA_ACT_SUBSCRIBE, HA_ACT_REJECTED };

struct HaProto {
  uint32_t subId = 1;        // id of the subscribe_entities request
  uint32_t histPending = 0;  // id of the outstanding history request, 0 = none
  uint32_t pingPending = 0;  // id of the outstanding ping, 0 = none
  bool authed = false;
};

// Reconnect delay: 5 s, doubling, capped at 5 min. next() returns the delay to wait now.
struct Backoff {
  uint32_t cur = 5000;
  uint32_t next() {
    uint32_t d = cur;
    cur = cur >= 150000 ? 300000 : cur * 2;
    return d;
  }
  void reset() { cur = 5000; }
};

// Attributes the models read: tram departures, light brightness and colour,
// thermostat temperatures and action.
inline void haFilterAttrs(JsonObject a) {
  a["departures"] = true;
  a["brightness"] = true; a["color_mode"] = true; a["color_temp_kelvin"] = true; a["hs_color"] = true;
  a["current_temperature"] = true; a["temperature"] = true; a["hvac_action"] = true;
  a["min_temp"] = true; a["max_temp"] = true;
}

// ArduinoJson filter for incoming messages: keeps only what the model reads
// (the departures sensor carries many other attributes).
inline void haBuildFilter(JsonDocument& f) {
  f["type"] = true; f["id"] = true; f["success"] = true; f["result"] = true;
  JsonObject ent = f["event"]["a"]["*"].to<JsonObject>();
  ent["s"] = true; ent["lc"] = true; ent["lu"] = true;
  haFilterAttrs(ent["a"].to<JsonObject>());
  JsonObject chg = f["event"]["c"]["*"].to<JsonObject>();
  JsonObject plus = chg["+"].to<JsonObject>();
  plus["s"] = true; plus["lc"] = true; plus["lu"] = true;
  haFilterAttrs(plus["a"].to<JsonObject>());
  chg["-"] = true;
}

inline HaAction haHandleMessage(HaProto& p, TramState& s, ClimateState& c, LightsState& L, HeatingState& H, PowerState& P, JsonVariantConst msg) {
  const char* type = msg["type"] | "";
  uint32_t id = msg["id"] | 0u;
  if (!strcmp(type, "auth_required")) return HA_ACT_SEND_AUTH;
  if (!strcmp(type, "auth_ok")) { p.authed = true; return HA_ACT_SUBSCRIBE; }
  if (!strcmp(type, "auth_invalid")) { p.authed = false; return HA_ACT_REJECTED; }
  if (!strcmp(type, "event") && id == p.subId) {
    tramApplyEvent(s, msg["event"]);
    climateApplyEvent(c, msg["event"]);
    lightsApplyEvent(L, msg["event"]);
    heatingApplyEvent(H, msg["event"]);
    powerApplyEvent(P, msg["event"]);
  } else if (!strcmp(type, "result") && id != 0 && id == p.histPending) {
    p.histPending = 0;
    if (msg["success"] | false) tramApplyHistory(s, msg["result"]);
  } else if (!strcmp(type, "pong") && id == p.pingPending) {
    p.pingPending = 0;
  }
  return HA_ACT_NONE;
}

// The subscribe_entities request (id 1) for every entity the pages show.
// Returns false if `n` is too small.
inline bool haBuildSubscribe(char* buf, size_t n) {
  size_t o = 0;
  bool first = true;
  auto add = [&](const char* s) {
    if (o < n) o += snprintf(buf + o, n - o, "%s", s);
  };
  auto addId = [&](const char* id) {
    add(first ? "\"" : ",\""); add(id); add("\"");
    first = false;
  };
  add("{\"id\":1,\"type\":\"subscribe_entities\",\"entity_ids\":[");
  addId(TRAM_ENTITY_DEPARTURES); addId(TRAM_ENTITY_GAP); addId(TRAM_ENTITY_LAST);
  for (const ClimateRoom& r : CLIMATE_ROOMS) { addId(r.temp); addId(r.hum); }
  for (const ClimatePm& pm : CLIMATE_PM) addId(pm.id);
  for (const LightDef& l : LIGHTS) addId(l.id);
  for (const ThermoDef& t : THERMOS) addId(t.id);
  addId(POWER_TOTAL_ID);
  for (const PowerDevice& d : POWER_DEVICES) addId(d.id);
  add("]}");
  return o < n;
}

// Inverse of daysFromCivil.
inline void civilFromDays(int32_t z, int32_t& y, uint32_t& m, uint32_t& d) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = (uint32_t)(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = (int32_t)yoe + era * 400 + (m <= 2);
}

// "YYYY-MM-DDTHH:MM:SSZ"
inline void formatIsoUtc(uint32_t epochUtc, char* buf, size_t n) {
  int32_t y; uint32_t m, d;
  civilFromDays((int32_t)(epochUtc / 86400), y, m, d);
  uint32_t r = epochUtc % 86400;
  snprintf(buf, n, "%04d-%02u-%02uT%02u:%02u:%02uZ", (int)y, (unsigned)m, (unsigned)d,
           (unsigned)(r / 3600), (unsigned)(r / 60 % 60), (unsigned)(r % 60));
}
