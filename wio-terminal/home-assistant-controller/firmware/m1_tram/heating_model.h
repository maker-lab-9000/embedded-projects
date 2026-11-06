// Heating page state and controls: two thermostats, on (heat) / off and the
// target temperature, sent as climate.set_hvac_mode / climate.set_temperature.
// Target changes are sent HEAT_DEBOUNCE_MS after the last press, so a run of
// presses makes one call to the (Zigbee) thermostat.
// Pure logic on ArduinoJson; host-tested (test/run.sh).
#pragma once
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include "sleep_ctl.h"  // Key

struct ThermoDef { const char* name; const char* id; };
static const ThermoDef THERMOS[] = {
  {"Theo's room", "climate.thermostat_14"},
  {"Living room", "climate.thermostat_15"},
};
#define THERMO_COUNT 2
#define HEAT_STEP 0.5f
#define HEAT_DEBOUNCE_MS 1000

enum HvacMode : uint8_t { HV_OFF, HV_HEAT, HV_AUTO, HV_OTHER };
struct ThermoState {
  bool known, available;
  HvacMode mode;
  bool heating;                 // hvac_action == "heating"
  bool hasCurrent; float current;
  bool hasTarget; float target;
  float minT, maxT;
};

enum HeatField : uint8_t { HF_POWER, HF_TARGET };
struct HeatingState {
  ThermoState t[THERMO_COUNT];
  uint8_t dirty;  // one bit per thermostat, cleared by the UI after drawing
  uint8_t sel;
  bool editing;
  HeatField field;
  bool uiDirty;
  bool pending; uint8_t pendingIdx; float pendingTarget; uint32_t pendingAt;
};

enum HeatActionKind : uint8_t { HA_NONE_ACT, HA_SET_MODE, HA_SET_TEMP };
struct HeatAction { HeatActionKind kind; uint8_t thermo; HvacMode mode; float temp; };

inline bool thermoIsOn(const ThermoState& t) { return t.available && t.mode != HV_OFF; }

// Target to show: the pending (not yet sent) one while the user is adjusting.
inline float thermoShownTarget(const HeatingState& H, int i) {
  return H.pending && H.pendingIdx == i ? H.pendingTarget : H.t[i].target;
}

inline void heatingApplyEntity(HeatingState& H, const char* id, JsonObjectConst st) {
  for (int i = 0; i < THERMO_COUNT; i++) {
    if (strcmp(id, THERMOS[i].id)) continue;
    ThermoState& t = H.t[i];
    if (st["s"].is<const char*>()) {
      const char* v = st["s"];
      t.known = true;
      t.available = strcmp(v, "unavailable") && strcmp(v, "unknown");
      if (t.available)
        t.mode = !strcmp(v, "off") ? HV_OFF : !strcmp(v, "heat") ? HV_HEAT : !strcmp(v, "auto") ? HV_AUTO : HV_OTHER;
    }
    JsonObjectConst a = st["a"];
    if (a["current_temperature"].is<float>()) { t.current = a["current_temperature"]; t.hasCurrent = true; }
    if (a["temperature"].is<float>()) { t.target = a["temperature"]; t.hasTarget = true; }
    if (a["min_temp"].is<float>()) t.minT = a["min_temp"];
    if (a["max_temp"].is<float>()) t.maxT = a["max_temp"];
    if (a["hvac_action"].is<const char*>()) t.heating = !strcmp(a["hvac_action"], "heating");
    H.dirty |= (uint8_t)(1u << i);
    return;
  }
}

inline void heatingApplyEvent(HeatingState& H, JsonVariantConst event) {
  for (JsonPairConst kv : event["a"].as<JsonObjectConst>())
    heatingApplyEntity(H, kv.key().c_str(), kv.value().as<JsonObjectConst>());
  for (JsonPairConst kv : event["c"].as<JsonObjectConst>())
    heatingApplyEntity(H, kv.key().c_str(), kv.value()["+"].as<JsonObjectConst>());
}

// Shows the change right away; HA's state push corrects it afterwards.
inline void heatingApplyAction(HeatingState& H, HeatAction a) {
  if (a.kind == HA_NONE_ACT || a.thermo >= THERMO_COUNT) return;
  ThermoState& t = H.t[a.thermo];
  if (a.kind == HA_SET_MODE) t.mode = a.mode;
  else { t.target = a.temp; t.hasTarget = true; }
  H.dirty |= (uint8_t)(1u << a.thermo);
}

// Turns a pending target into the call to send now.
inline bool heatFlush(HeatingState& H, HeatAction& out) {
  if (!H.pending) return false;
  H.pending = false;
  out = {HA_SET_TEMP, H.pendingIdx, HV_OFF, H.pendingTarget};
  heatingApplyAction(H, out);
  return true;
}

// Called every loop: the pending target, once the presses have stopped.
inline bool heatingDue(HeatingState& H, uint32_t ms, HeatAction& out) {
  if (!H.pending || ms - H.pendingAt < HEAT_DEBOUNCE_MS) return false;
  return heatFlush(H, out);
}

// Joystick and buttons on the Heating page; same scheme as the Lights page.
// Returns true when the key was used; `out` is the call to make now.
inline bool heatingOnKey(HeatingState& H, Key k, uint32_t ms, HeatAction& out) {
  out = {HA_NONE_ACT, H.sel, HV_OFF, 0};
  if (!H.editing) {
    switch (k) {
      case KEY_JOY_UP:    heatFlush(H, out); if (H.sel > 0) H.sel--; break;
      case KEY_JOY_DOWN:  heatFlush(H, out); if (H.sel + 1 < THERMO_COUNT) H.sel++; break;
      case KEY_JOY_PRESS: H.editing = true; H.field = HF_POWER; break;
      default: return false;
    }
    H.uiDirty = true;
    return true;
  }
  const ThermoState& t = H.t[H.sel];
  bool up = k == KEY_JOY_RIGHT;
  switch (k) {
    case KEY_JOY_UP:   H.field = HF_POWER; H.uiDirty = true; return true;
    case KEY_JOY_DOWN: H.field = HF_TARGET; H.uiDirty = true; return true;
    case KEY_JOY_PRESS: case KEY_B: H.editing = false; H.uiDirty = true; return true;
    case KEY_JOY_LEFT: case KEY_JOY_RIGHT: break;
    default: return false;
  }
  if (!t.available) return true;
  if (H.field == HF_POWER) {
    out.kind = HA_SET_MODE;
    out.mode = thermoIsOn(t) ? HV_OFF : HV_HEAT;
    return true;
  }
  float cur = H.pending && H.pendingIdx == H.sel ? H.pendingTarget : (t.hasTarget ? t.target : 20.0f);
  float next = cur + (up ? HEAT_STEP : -HEAT_STEP);
  float lo = t.minT > 0 ? t.minT : 7, hi = t.maxT > 0 ? t.maxT : 35;
  if (next < lo) next = lo;
  if (next > hi) next = hi;
  H.pending = true; H.pendingIdx = H.sel; H.pendingTarget = next; H.pendingAt = ms;
  H.dirty |= (uint8_t)(1u << H.sel);
  return true;
}

// The call_service request for `a`. Returns false for HA_NONE_ACT or if `n` is too small.
inline bool heatBuildCall(char* buf, size_t n, uint32_t id, HeatAction a) {
  if (a.kind == HA_NONE_ACT || a.thermo >= THERMO_COUNT) return false;
  char data[40];
  const char* service;
  if (a.kind == HA_SET_MODE) {
    service = "set_hvac_mode";
    snprintf(data, sizeof(data), "{\"hvac_mode\":\"%s\"}", a.mode == HV_OFF ? "off" : a.mode == HV_AUTO ? "auto" : "heat");
  } else {
    service = "set_temperature";
    snprintf(data, sizeof(data), "{\"temperature\":%.1f}", (double)a.temp);
  }
  int w = snprintf(buf, n,
                   "{\"id\":%lu,\"type\":\"call_service\",\"domain\":\"climate\",\"service\":\"%s\","
                   "\"service_data\":%s,\"target\":{\"entity_id\":\"%s\"}}",
                   (unsigned long)id, service, data, THERMOS[a.thermo].id);
  return w > 0 && (size_t)w < n;
}
