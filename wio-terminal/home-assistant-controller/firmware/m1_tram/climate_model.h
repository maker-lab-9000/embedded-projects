// Climate page state: temperature and humidity per room, plus air quality
// (PM2.5, PM10), from the "Temperature / Humidity (colour preview)" and
// "Air quality" cards of the Control Panel dashboard.
// Pure logic on ArduinoJson; host-tested (test/run.sh).
#pragma once
#include <ArduinoJson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ClimateRoom { const char* name; const char* temp; const char* hum; };
struct ClimatePm   { const char* name; const char* id; float limit; };

static const ClimateRoom CLIMATE_ROOMS[] = {
  {"Bedroom",     "sensor.multi_sensor_bedzimmer", "sensor.multi_sensor_bedzimmer_2"},
  {"Theo's room", "sensor.babyroom",               "sensor.babyroom_2"},
  {"Bathroom",    "sensor.temperature_9",          "sensor.humidity_10"},
  {"Living room", "sensor.living_room",            "sensor.living_room_2"},
};
// Limits from the card names ("PM2.5 ( <10 )", "PM10 ( <20 )").
static const ClimatePm CLIMATE_PM[] = {
  {"PM2.5", "sensor.nova_pm2_5", 10},
  {"PM10",  "sensor.nova_pm10",  20},
};
#define CLIMATE_ROOM_COUNT 4
#define CLIMATE_PM_COUNT   2

struct Reading { float v; bool known; };
struct ClimateState {
  Reading temp[CLIMATE_ROOM_COUNT], hum[CLIMATE_ROOM_COUNT], pm[CLIMATE_PM_COUNT];
  uint8_t dirty;  // one bit per row, cleared by the UI after drawing
};
#define CLIMATE_DIRTY_ROOM(i) (uint8_t)(1u << (i))
#define CLIMATE_DIRTY_PM(i)   (uint8_t)(1u << (CLIMATE_ROOM_COUNT + (i)))

enum ClimateColour : uint8_t { CL_GREY, CL_BLUE, CL_GREEN, CL_YELLOW, CL_RED };

// Same thresholds as the dashboard's icon_color templates.
inline ClimateColour tempColour(Reading r) {
  if (!r.known) return CL_GREY;
  return r.v > 26 ? CL_RED : r.v >= 20 ? CL_YELLOW : CL_BLUE;
}
inline ClimateColour humColour(Reading r) {
  if (!r.known) return CL_GREY;
  return r.v > 75 ? CL_RED : r.v > 65 ? CL_YELLOW : CL_GREEN;
}
// Within the card's limit green, up to twice the limit yellow, above red.
inline ClimateColour pmColour(Reading r, float limit) {
  if (!r.known) return CL_GREY;
  return r.v > 2 * limit ? CL_RED : r.v >= limit ? CL_YELLOW : CL_GREEN;
}

inline void climateFormat(Reading r, int decimals, char* buf, size_t n) {
  if (!r.known) snprintf(buf, n, "--");
  else snprintf(buf, n, "%.*f", decimals, (double)r.v);
}

inline void climateSet(Reading& r, const char* s) {
  char* end;
  float f = strtof(s, &end);
  r.known = end != s && *end == 0;  // "unavailable", "unknown" -> not known
  if (r.known) r.v = f;
}

// Applies one entity's compressed state; changes without "s" are ignored.
inline void climateApplyEntity(ClimateState& c, const char* id, JsonObjectConst st) {
  if (!st["s"].is<const char*>()) return;
  const char* s = st["s"];
  for (int i = 0; i < CLIMATE_ROOM_COUNT; i++) {
    if (!strcmp(id, CLIMATE_ROOMS[i].temp)) { climateSet(c.temp[i], s); c.dirty |= CLIMATE_DIRTY_ROOM(i); return; }
    if (!strcmp(id, CLIMATE_ROOMS[i].hum))  { climateSet(c.hum[i], s);  c.dirty |= CLIMATE_DIRTY_ROOM(i); return; }
  }
  for (int i = 0; i < CLIMATE_PM_COUNT; i++)
    if (!strcmp(id, CLIMATE_PM[i].id)) { climateSet(c.pm[i], s); c.dirty |= CLIMATE_DIRTY_PM(i); return; }
}

// `event` is the "event" object of a subscribe_entities message.
inline void climateApplyEvent(ClimateState& c, JsonVariantConst event) {
  for (JsonPairConst kv : event["a"].as<JsonObjectConst>())
    climateApplyEntity(c, kv.key().c_str(), kv.value().as<JsonObjectConst>());
  for (JsonPairConst kv : event["c"].as<JsonObjectConst>())
    climateApplyEntity(c, kv.key().c_str(), kv.value()["+"].as<JsonObjectConst>());
}
