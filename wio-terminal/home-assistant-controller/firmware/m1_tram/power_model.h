// Power page state: "Using now · all plugs" and the per-device plugs from
// the Power dashboard (tiles under "Devices"), in watts.
// Pure logic on ArduinoJson; host-tested (test/run.sh).
#pragma once
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include "climate_model.h"  // Reading, climateSet

#define POWER_TOTAL_ID "sensor.home_plugs_total_power"

struct PowerDevice { const char* name; const char* id; };
static const PowerDevice POWER_DEVICES[] = {
  {"Homelab",         "sensor.power_23"},
  {"Workstation",     "sensor.wiz_socket_power"},
  {"Washing machine", "sensor.wasching_machine_plug"},
  {"Dishwasher",      "sensor.power_36"},
  {"Fridge",          "sensor.power_27"},
  {"TV & Audio",      "sensor.power_30"},
  {"TV & Audio 2",    "sensor.power_32"},
};
#define POWER_DEVICE_COUNT 7

struct PowerState {
  Reading total;
  Reading dev[POWER_DEVICE_COUNT];
  uint8_t dirty;  // one bit per device, top bit for the total; cleared by the UI
};
#define POWER_DIRTY_DEV(i)  (uint8_t)(1u << (i))
#define POWER_DIRTY_TOTAL   (uint8_t)0x80

// Whole watts, or "--" when unknown.
inline void powerFormat(Reading r, char* buf, size_t n) {
  if (!r.known) snprintf(buf, n, "--");
  else snprintf(buf, n, "%.0f", (double)r.v);
}

// Device's share of the total, 0..1, for the bar; 0 when either is unknown.
inline float powerShare(Reading dev, Reading total) {
  if (!dev.known || !total.known || total.v <= 0 || dev.v <= 0) return 0;
  float s = dev.v / total.v;
  return s > 1 ? 1 : s;
}

// Applies one entity's compressed state; changes without "s" are ignored.
inline void powerApplyEntity(PowerState& P, const char* id, JsonObjectConst st) {
  if (!st["s"].is<const char*>()) return;
  const char* s = st["s"];
  if (!strcmp(id, POWER_TOTAL_ID)) { climateSet(P.total, s); P.dirty |= POWER_DIRTY_TOTAL; return; }
  for (int i = 0; i < POWER_DEVICE_COUNT; i++)
    if (!strcmp(id, POWER_DEVICES[i].id)) { climateSet(P.dev[i], s); P.dirty |= POWER_DIRTY_DEV(i); return; }
}

inline void powerApplyEvent(PowerState& P, JsonVariantConst event) {
  for (JsonPairConst kv : event["a"].as<JsonObjectConst>())
    powerApplyEntity(P, kv.key().c_str(), kv.value().as<JsonObjectConst>());
  for (JsonPairConst kv : event["c"].as<JsonObjectConst>())
    powerApplyEntity(P, kv.key().c_str(), kv.value()["+"].as<JsonObjectConst>());
}
