// Tram page state, updated from Home Assistant websocket messages.
// Pure logic on ArduinoJson; host-tested (test/run.sh).
#pragma once
#include <ArduinoJson.h>
#include <string.h>
#include <stdlib.h>
#include "timeutil.h"

#define TRAM_ENTITY_DEPARTURES "sensor.kuckhoffstr_berlin"
#define TRAM_ENTITY_GAP        "input_number.home_m1_tram_minutes_between_trams"
#define TRAM_ENTITY_LAST       "sensor.home_m1_tram_last_departed"
#define TRAM_MAX_DEPS 3
#define TRAM_MAX_HIST 96

struct Departure { char line[6]; char dir[40]; char time[6]; uint32_t epoch; int16_t delayMin; uint16_t color565; };
struct GapPoint  { uint32_t t; float v; };
struct TramState {
  Departure dep[TRAM_MAX_DEPS]; uint8_t depCount; bool depKnown;
  float gapMin; bool gapKnown;
  uint32_t lastDeparted;            // 0 = unknown
  GapPoint hist[TRAM_MAX_HIST]; uint8_t histCount; bool histKnown;
  uint8_t dirty;                    // DIRTY_* bits, cleared by the UI after drawing
};
enum : uint8_t { DIRTY_NEXT = 1, DIRTY_GAP = 2, DIRTY_LAST = 4, DIRTY_CHART = 8 };

inline uint16_t hexTo565(const char* hex) {
  if (!hex || hex[0] != '#' || strlen(hex) != 7) return 0xF800;
  char* end;
  uint32_t v = strtoul(hex + 1, &end, 16);
  if (*end) return 0xF800;
  uint8_t r = v >> 16, g = v >> 8, b = v;
  return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}

// Copies `src` into `dst` (size n), folding German umlauts/ß to ASCII and
// replacing other non-ASCII bytes with '?', since the TFT fonts are ASCII-only.
inline void asciiFold(const char* src, char* dst, size_t n) {
  size_t o = 0;
  const unsigned char* p = (const unsigned char*)(src ? src : "");
  auto put = [&](const char* s) { while (*s && o + 1 < n) dst[o++] = *s++; };
  while (*p && o + 1 < n) {
    if (*p < 0x80) { dst[o++] = (char)*p++; continue; }
    if (p[0] == 0xC3 && p[1]) {
      switch (p[1]) {
        case 0xA4: put("ae"); break; case 0xB6: put("oe"); break; case 0xBC: put("ue"); break;
        case 0x84: put("Ae"); break; case 0x96: put("Oe"); break; case 0x9C: put("Ue"); break;
        case 0x9F: put("ss"); break; default: put("?");
      }
      p += 2;
      continue;
    }
    put("?");
    p++;
    while ((*p & 0xC0) == 0x80) p++;  // skip continuation bytes
  }
  dst[o] = 0;
}

inline void tramSetDepartures(TramState& s, JsonArrayConst deps) {
  s.depCount = 0;
  s.depKnown = true;
  for (JsonObjectConst d : deps) {
    if (s.depCount >= TRAM_MAX_DEPS) break;
    if (d["cancelled"] | false) continue;
    uint32_t e;
    if (!parseIso8601(d["timestamp"] | "", e)) continue;
    Departure& o = s.dep[s.depCount++];
    asciiFold(d["line_name"] | "?", o.line, sizeof(o.line));
    asciiFold(d["direction"] | "", o.dir, sizeof(o.dir));
    asciiFold(d["time"] | "", o.time, sizeof(o.time));
    o.epoch = e;
    o.delayMin = (int16_t)((d["delay"] | 0) / 60);
    o.color565 = hexTo565(d["color"] | "#D82020");
  }
  s.dirty |= DIRTY_NEXT;
}

// Applies one entity's state fields (`st`: compressed object with "s" and/or "a").
inline void tramApplyEntity(TramState& s, const char* id, JsonObjectConst st) {
  if (!strcmp(id, TRAM_ENTITY_DEPARTURES)) {
    JsonVariantConst deps = st["a"]["departures"];
    if (deps.is<JsonArrayConst>()) tramSetDepartures(s, deps.as<JsonArrayConst>());
  } else if (!strcmp(id, TRAM_ENTITY_GAP)) {
    if (!st["s"].is<const char*>()) return;
    const char* v = st["s"];
    char* end;
    float f = strtof(v, &end);
    s.gapKnown = end != v && *end == 0;
    if (s.gapKnown) s.gapMin = f;
    s.dirty |= DIRTY_GAP;
    // Live change: extend the chart now instead of waiting for the next history fetch.
    double t = st["lc"] | (st["lu"] | 0.0);
    if (s.gapKnown && s.histKnown && t > 0 &&
        (s.histCount == 0 || (uint32_t)t > s.hist[s.histCount - 1].t)) {
      if (s.histCount == TRAM_MAX_HIST) {
        memmove(s.hist, s.hist + 1, sizeof(GapPoint) * (TRAM_MAX_HIST - 1));
        s.histCount--;
      }
      s.hist[s.histCount++] = {(uint32_t)t, f};
      s.dirty |= DIRTY_CHART;
    }
  } else if (!strcmp(id, TRAM_ENTITY_LAST)) {
    if (!st["s"].is<const char*>()) return;
    uint32_t e;
    s.lastDeparted = parseIso8601(st["s"], e) ? e : 0;
    s.dirty |= DIRTY_LAST;
  }
}

// `event` is the "event" object of a subscribe_entities message.
inline void tramApplyEvent(TramState& s, JsonVariantConst event) {
  for (JsonPairConst kv : event["a"].as<JsonObjectConst>())
    tramApplyEntity(s, kv.key().c_str(), kv.value().as<JsonObjectConst>());
  for (JsonPairConst kv : event["c"].as<JsonObjectConst>())
    tramApplyEntity(s, kv.key().c_str(), kv.value()["+"].as<JsonObjectConst>());
}

inline bool histPoint(JsonObjectConst p, GapPoint& out) {
  const char* v = p["s"] | "";
  char* end;
  float f = strtof(v, &end);
  if (end == v || *end) return false;
  double t = p["lu"] | (p["lc"] | 0.0);
  if (t <= 0) return false;
  out.t = (uint32_t)t;
  out.v = f;
  return true;
}

// `result` is the "result" object of a history/history_during_period reply.
// Points are chronological; when more than TRAM_MAX_HIST are numeric, keep the latest.
inline void tramApplyHistory(TramState& s, JsonVariantConst result) {
  JsonArrayConst pts = result[TRAM_ENTITY_GAP].as<JsonArrayConst>();
  GapPoint g;
  size_t total = 0;
  for (JsonObjectConst p : pts) if (histPoint(p, g)) total++;
  size_t skip = total > TRAM_MAX_HIST ? total - TRAM_MAX_HIST : 0;
  s.histCount = 0;
  for (JsonObjectConst p : pts) {
    if (!histPoint(p, g)) continue;
    if (skip) { skip--; continue; }
    s.hist[s.histCount++] = g;
  }
  s.histKnown = true;
  s.dirty |= DIRTY_CHART;
}
