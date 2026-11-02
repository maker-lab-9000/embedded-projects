// Lights page state and controls: three lamps, on/off, brightness and colour
// presets, sent to Home Assistant as light.toggle / light.turn_on.
// Pure logic on ArduinoJson; host-tested (test/run.sh).
#pragma once
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "sleep_ctl.h"  // Key

struct LightDef { const char* name; const char* id; };
static const LightDef LIGHTS[] = {
  {"Philips lamp", "light.philips_lamp"},
  {"Tripod lamp",  "light.extended_color_light_10"},
  {"Moon lamp",    "light.extended_color_light_8"},
};
#define LIGHT_COUNT 3

// kelvin != 0: white preset (color_temp_kelvin); otherwise hue/saturation.
struct ColourPreset { const char* name; uint16_t kelvin; uint16_t hue; uint8_t sat; };
static const ColourPreset COLOUR_PRESETS[] = {
  {"Warm white", 2700, 0, 0}, {"Neutral", 4000, 0, 0}, {"Cool white", 6500, 0, 0},
  {"Red", 0, 0, 100}, {"Orange", 0, 30, 100}, {"Yellow", 0, 55, 100}, {"Green", 0, 120, 100},
  {"Cyan", 0, 180, 100}, {"Blue", 0, 230, 100}, {"Purple", 0, 275, 100}, {"Pink", 0, 320, 70},
};
#define COLOUR_PRESET_COUNT 11

enum LightMode : uint8_t { LM_NONE, LM_CT, LM_HS };
struct LightState {
  bool known, available, on;
  uint8_t brightness;  // 0..255 as HA reports it
  LightMode mode;
  uint16_t kelvin;
  float hue, sat;
};

enum LightField : uint8_t { LF_POWER, LF_BRIGHTNESS, LF_COLOUR };
struct LightsState {
  LightState l[LIGHT_COUNT];
  uint8_t dirty;    // one bit per lamp, cleared by the UI after drawing
  uint8_t sel;      // selected lamp
  bool editing;     // settings of the selected lamp have the joystick
  LightField field;
  bool uiDirty;     // selection or focus moved
};

enum LightActionKind : uint8_t { LA_NONE, LA_TOGGLE, LA_BRIGHTNESS, LA_COLOUR };
struct LightAction { LightActionKind kind; uint8_t light; uint8_t pct; uint8_t preset; };

inline uint8_t lightPct(uint8_t b) {
  if (!b) return 0;
  int p = (int)lroundf(b * 100.0f / 255.0f);
  return (uint8_t)(p < 1 ? 1 : p);
}

// Next brightness in 10 % steps; down from 10 % goes to 1 % (still on).
inline uint8_t lightStepPct(uint8_t pct, bool up) {
  if (up) return pct < 10 ? 10 : (uint8_t)(pct - pct % 10 + 10 > 100 ? 100 : pct - pct % 10 + 10);
  if (pct <= 10) return 1;
  return (uint8_t)(pct % 10 ? pct - pct % 10 : pct - 10);
}

// Preset the lamp's colour matches, or -1 ("Custom").
inline int lightPresetIndex(const LightState& s) {
  int best = -1;
  float bestD = 0;
  for (int i = 0; i < COLOUR_PRESET_COUNT; i++) {
    const ColourPreset& p = COLOUR_PRESETS[i];
    float d;
    if (s.mode == LM_CT && p.kelvin) {
      d = fabsf((float)s.kelvin - p.kelvin);
      if (d > 400) continue;
    } else if (s.mode == LM_HS && !p.kelvin && s.sat >= 40) {
      d = fabsf(s.hue - p.hue);
      if (d > 180) d = 360 - d;
      if (d > 20) continue;
    } else {
      continue;
    }
    if (best < 0 || d < bestD) { best = i; bestD = d; }
  }
  return best;
}

inline uint16_t rgb565(float r, float g, float b) {
  auto c = [](float v, int bits) { int m = (1 << bits) - 1; int x = (int)lroundf(v * m); return x < 0 ? 0 : x > m ? m : x; };
  return (uint16_t)(c(r, 5) << 11 | c(g, 6) << 5 | c(b, 5));
}

// Swatch colour of the lamp's current light; 0 when off or unavailable.
inline uint16_t lightSwatch565(const LightState& s) {
  if (!s.available || !s.on) return 0;
  if (s.mode == LM_HS) {
    float h = fmodf(s.hue, 360.0f) / 60.0f, v = 1.0f, sat = s.sat / 100.0f;
    float c = v * sat, x = c * (1 - fabsf(fmodf(h, 2.0f) - 1)), m = v - c;
    float r = 0, g = 0, b = 0;
    switch ((int)h) {
      case 0: r = c; g = x; break; case 1: r = x; g = c; break; case 2: g = c; b = x; break;
      case 3: g = x; b = c; break; case 4: r = x; b = c; break; default: r = c; b = x;
    }
    return rgb565(r + m, g + m, b + m);
  }
  if (s.mode == LM_CT) {  // warm 2000 K (1, .54, .05) to cold 6500 K (1, .98, 1)
    float t = ((float)s.kelvin - 2000) / 4500;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return rgb565(1, 0.54f + 0.44f * t, 0.05f + 0.95f * t);
  }
  return rgb565(1, 0.85f, 0.5f);  // on/off-only lamp: plain warm
}

// Applies one lamp's compressed state ("s" and/or "a"); null attributes
// (HA sends them when a lamp turns off) leave the stored value.
inline void lightsApplyEntity(LightsState& L, const char* id, JsonObjectConst st) {
  for (int i = 0; i < LIGHT_COUNT; i++) {
    if (strcmp(id, LIGHTS[i].id)) continue;
    LightState& s = L.l[i];
    if (st["s"].is<const char*>()) {
      const char* v = st["s"];
      s.available = strcmp(v, "unavailable") && strcmp(v, "unknown");
      s.on = !strcmp(v, "on");
      s.known = true;
    }
    JsonObjectConst a = st["a"];
    if (a["brightness"].is<int>()) s.brightness = (uint8_t)a["brightness"].as<int>();
    if (a["color_temp_kelvin"].is<int>()) s.kelvin = (uint16_t)a["color_temp_kelvin"].as<int>();
    if (a["hs_color"].is<JsonArrayConst>()) { s.hue = a["hs_color"][0] | 0.0f; s.sat = a["hs_color"][1] | 0.0f; }
    if (a["color_mode"].is<const char*>()) {
      const char* m = a["color_mode"];
      s.mode = !strcmp(m, "color_temp") ? LM_CT
             : (!strcmp(m, "hs") || !strcmp(m, "xy") || !strncmp(m, "rgb", 3)) ? LM_HS : LM_NONE;
    }
    L.dirty |= (uint8_t)(1u << i);
    return;
  }
}

inline void lightsApplyEvent(LightsState& L, JsonVariantConst event) {
  for (JsonPairConst kv : event["a"].as<JsonObjectConst>())
    lightsApplyEntity(L, kv.key().c_str(), kv.value().as<JsonObjectConst>());
  for (JsonPairConst kv : event["c"].as<JsonObjectConst>())
    lightsApplyEntity(L, kv.key().c_str(), kv.value()["+"].as<JsonObjectConst>());
}

// Joystick and buttons on the Lights page. Returns true when the key was
// used; `out` is the service call to make (LA_NONE for none). In the list,
// left/right and A are left to the page switcher.
inline bool lightsOnKey(LightsState& L, Key k, LightAction& out) {
  out = {LA_NONE, L.sel, 0, 0};
  if (!L.editing) {
    switch (k) {
      case KEY_JOY_UP:    if (L.sel > 0) L.sel--; break;
      case KEY_JOY_DOWN:  if (L.sel + 1 < LIGHT_COUNT) L.sel++; break;
      case KEY_JOY_PRESS: L.editing = true; L.field = LF_POWER; break;
      default: return false;
    }
    out.light = L.sel;
    L.uiDirty = true;
    return true;
  }
  const LightState& s = L.l[L.sel];
  bool up = k == KEY_JOY_RIGHT;
  switch (k) {
    case KEY_JOY_UP:   if (L.field > LF_POWER) L.field = (LightField)(L.field - 1); L.uiDirty = true; return true;
    case KEY_JOY_DOWN: if (L.field < LF_COLOUR) L.field = (LightField)(L.field + 1); L.uiDirty = true; return true;
    case KEY_JOY_PRESS: case KEY_B: L.editing = false; L.uiDirty = true; return true;
    case KEY_JOY_LEFT: case KEY_JOY_RIGHT: break;
    default: return false;
  }
  if (!s.available) return true;
  if (L.field == LF_POWER) {
    out.kind = LA_TOGGLE;
  } else if (L.field == LF_BRIGHTNESS) {
    out.kind = LA_BRIGHTNESS;
    out.pct = lightStepPct(s.on ? lightPct(s.brightness) : 0, up);
  } else {
    int cur = lightPresetIndex(s);
    out.kind = LA_COLOUR;
    out.preset = (uint8_t)(cur < 0 ? (up ? 0 : COLOUR_PRESET_COUNT - 1)
                                   : (cur + (up ? 1 : COLOUR_PRESET_COUNT - 1)) % COLOUR_PRESET_COUNT);
  }
  return true;
}

// Shows the change right away; HA's state push corrects it afterwards.
inline void lightsApplyAction(LightsState& L, LightAction a) {
  if (a.kind == LA_NONE || a.light >= LIGHT_COUNT) return;
  LightState& s = L.l[a.light];
  if (a.kind == LA_TOGGLE) {
    s.on = !s.on;
    if (s.on && !s.brightness) s.brightness = 255;
  } else if (a.kind == LA_BRIGHTNESS) {
    s.on = true;
    s.brightness = (uint8_t)lroundf(a.pct * 255.0f / 100.0f);
  } else {
    const ColourPreset& p = COLOUR_PRESETS[a.preset % COLOUR_PRESET_COUNT];
    s.on = true;
    if (!s.brightness) s.brightness = 255;
    if (p.kelvin) { s.mode = LM_CT; s.kelvin = p.kelvin; }
    else { s.mode = LM_HS; s.hue = p.hue; s.sat = p.sat; }
  }
  L.dirty |= (uint8_t)(1u << a.light);
}

// The call_service request for `a`. Returns false for LA_NONE or if `n` is too small.
inline bool lightBuildCall(char* buf, size_t n, uint32_t id, LightAction a) {
  if (a.kind == LA_NONE || a.light >= LIGHT_COUNT) return false;
  char data[48];
  const char* service = "turn_on";
  if (a.kind == LA_TOGGLE) {
    service = "toggle";
    snprintf(data, sizeof(data), "{}");
  } else if (a.kind == LA_BRIGHTNESS) {
    snprintf(data, sizeof(data), "{\"brightness_pct\":%u}", (unsigned)a.pct);
  } else {
    const ColourPreset& p = COLOUR_PRESETS[a.preset % COLOUR_PRESET_COUNT];
    if (p.kelvin) snprintf(data, sizeof(data), "{\"color_temp_kelvin\":%u}", (unsigned)p.kelvin);
    else snprintf(data, sizeof(data), "{\"hs_color\":[%u,%u]}", (unsigned)p.hue, (unsigned)p.sat);
  }
  int w = snprintf(buf, n,
                   "{\"id\":%lu,\"type\":\"call_service\",\"domain\":\"light\",\"service\":\"%s\","
                   "\"service_data\":%s,\"target\":{\"entity_id\":\"%s\"}}",
                   (unsigned long)id, service, data, LIGHTS[a.light].id);
  return w > 0 && (size_t)w < n;
}
