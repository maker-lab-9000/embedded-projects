// Homelab page state: Proxmox host CPU and RAM usage, and the five
// temperatures of the "Temperatures, last 24 hours" card as 15-minute means.
// The sensors keep no statistics and a day of raw history is ~100 KB, so the
// day is loaded in 2-hour pieces (one sensor at a time) and averaged here;
// after that, live changes from the subscription extend it.
// Pure logic on ArduinoJson; host-tested (test/run.sh).
#pragma once
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
#include "climate_model.h"  // Reading, climateSet
#include "chart.h"          // ChartScale, GapPoint, histPoint, hexTo565

#define HL_CPU_ID "sensor.proxmox_cpu_usage"
#define HL_RAM_ID "sensor.proxmox_ram_usage"

struct HlTemp { const char* name; const char* id; const char* colour; };
static const HlTemp HL_TEMPS[] = {
  {"CPU",        "sensor.cpu_temp",        "#e07a2e"},
  {"GPU",        "sensor.gpu_temp",        "#c9a227"},
  {"NVMe",       "sensor.nvme_temp",       "#3a6fd8"},
  {"BigData",    "sensor.bigdata_temp",    "#2f9e8f"},
  {"JellyMedia", "sensor.jellymedia_temp", "#8a94a3"},
};
#define HL_TEMP_COUNT 5
#define HL_BUCKET_S   900                  // 15 min, as the card's points_per_hour: 4
#define HL_BUCKETS    96                   // 24 h
#define HL_WINDOW_S   7200                 // history piece: CPU temp is ~10 KB per 2 h
#define HL_WINDOWS    (86400 / HL_WINDOW_S)
#define HL_REQUESTS   (HL_WINDOWS * HL_TEMP_COUNT)

struct HlSeries { float sum[HL_BUCKETS]; uint16_t n[HL_BUCKETS]; };  // slot = bucket % HL_BUCKETS

#define HL_DIRTY_CPU   0x01
#define HL_DIRTY_RAM   0x02
#define HL_DIRTY_TEMPS 0x04
#define HL_DIRTY_CHART 0x08

struct HomelabState {
  Reading cpu, ram, temp[HL_TEMP_COUNT];
  HlSeries s[HL_TEMP_COUNT];
  uint32_t head;      // newest bucket number (t / HL_BUCKET_S)
  uint32_t fillTo;    // history loaded up to here; 0 = not started
  uint8_t fillNext;   // next history piece; HL_REQUESTS = all loaded
  uint8_t dirty;
};

// Same thresholds as the card's color_thresholds.
inline uint16_t hlUsage565(Reading r) {
  if (!r.known) return 0;
  return hexTo565(r.v >= 90 ? "#d1453b" : r.v >= 75 ? "#c98a0b" : "#3a6fd8");
}

// Moves the newest bucket forward, clearing the slots it reuses.
inline void hlAdvance(HomelabState& H, uint32_t bucket) {
  if (bucket <= H.head) return;
  uint32_t steps = bucket - H.head;
  if (steps > HL_BUCKETS) steps = HL_BUCKETS;
  for (uint32_t k = 0; k < steps; k++) {
    uint32_t slot = (bucket - k) % HL_BUCKETS;
    for (int i = 0; i < HL_TEMP_COUNT; i++) { H.s[i].sum[slot] = 0; H.s[i].n[slot] = 0; }
  }
  H.head = bucket;
  H.dirty |= HL_DIRTY_CHART;
}

inline void hlAddSample(HomelabState& H, int i, uint32_t t, float v) {
  uint32_t b = t / HL_BUCKET_S;
  hlAdvance(H, b);
  if (H.head - b >= HL_BUCKETS) return;  // older than 24 h
  H.s[i].sum[b % HL_BUCKETS] += v;
  H.s[i].n[b % HL_BUCKETS]++;
  H.dirty |= HL_DIRTY_CHART;
}

// Starts (or restarts, after a reconnect) loading the last 24 h up to `now`.
inline void hlBeginFill(HomelabState& H, uint32_t now) {
  memset(H.s, 0, sizeof(H.s));
  H.head = now / HL_BUCKET_S;
  H.fillTo = now;
  H.fillNext = 0;
  H.dirty |= HL_DIRTY_CHART;
}

// The next history piece to ask for: newest window first, all sensors per window.
inline bool hlNextRequest(const HomelabState& H, uint8_t& sensor, uint32_t& start, uint32_t& end) {
  if (!H.fillTo || H.fillNext >= HL_REQUESTS) return false;
  uint32_t w = H.fillNext / HL_TEMP_COUNT;
  sensor = H.fillNext % HL_TEMP_COUNT;
  end = H.fillTo - w * HL_WINDOW_S;
  start = end - HL_WINDOW_S;
  return true;
}

inline void hlFillAdvance(HomelabState& H) {
  if (H.fillNext < HL_REQUESTS) H.fillNext++;
}

// `result` of a history/history_during_period reply; points from fillTo on
// are left to the live updates.
inline void homelabApplyHistory(HomelabState& H, JsonVariantConst result) {
  for (int i = 0; i < HL_TEMP_COUNT; i++) {
    GapPoint g;
    for (JsonObjectConst p : result[HL_TEMPS[i].id].as<JsonArrayConst>())
      if (histPoint(p, g) && g.t < H.fillTo) hlAddSample(H, i, g.t, g.v);
  }
}

inline void homelabApplyEntity(HomelabState& H, const char* id, JsonObjectConst st) {
  if (!st["s"].is<const char*>()) return;
  const char* s = st["s"];
  if (!strcmp(id, HL_CPU_ID)) { climateSet(H.cpu, s); H.dirty |= HL_DIRTY_CPU; return; }
  if (!strcmp(id, HL_RAM_ID)) { climateSet(H.ram, s); H.dirty |= HL_DIRTY_RAM; return; }
  for (int i = 0; i < HL_TEMP_COUNT; i++) {
    if (strcmp(id, HL_TEMPS[i].id)) continue;
    climateSet(H.temp[i], s);
    H.dirty |= HL_DIRTY_TEMPS;
    double t = st["lc"] | (st["lu"] | 0.0);
    if (H.fillTo && H.temp[i].known && t >= H.fillTo) hlAddSample(H, i, (uint32_t)t, H.temp[i].v);
    return;
  }
}

inline void homelabApplyEvent(HomelabState& H, JsonVariantConst event) {
  for (JsonPairConst kv : event["a"].as<JsonObjectConst>())
    homelabApplyEntity(H, kv.key().c_str(), kv.value().as<JsonObjectConst>());
  for (JsonPairConst kv : event["c"].as<JsonObjectConst>())
    homelabApplyEntity(H, kv.key().c_str(), kv.value()["+"].as<JsonObjectConst>());
}

// Sensor i's value per bucket, oldest first, ending at `head`. Buckets without
// samples keep the previous value (the sensor held it); NAN before the first.
// Returns how many are known.
inline int hlPoints(const HomelabState& H, int i, float* out) {
  float last = NAN;
  int known = 0;
  for (int k = 0; k < HL_BUCKETS; k++) {
    uint32_t b = H.head - (HL_BUCKETS - 1) + k;
    uint32_t slot = b % HL_BUCKETS;
    if (H.head >= (uint32_t)(HL_BUCKETS - 1 - k) && H.s[i].n[slot]) last = H.s[i].sum[slot] / H.s[i].n[slot];
    out[k] = last;
    if (!isnan(last)) known++;
  }
  return known;
}

// Lower bound 15 °C as on the card (lower in steps of 5 if a value is
// below); upper bound one 5 °C step above the highest value, rounded.
inline ChartScale hlScale(const HomelabState& H) {
  bool any = false;
  float lo = 0, hi = 0;
  for (int i = 0; i < HL_TEMP_COUNT; i++)
    for (int k = 0; k < HL_BUCKETS; k++) {
      if (!H.s[i].n[k]) continue;
      float v = H.s[i].sum[k] / H.s[i].n[k];
      if (!any || v < lo) lo = v;
      if (!any || v > hi) hi = v;
      any = true;
    }
  if (!any) return {15, 65};
  int a = lo < 15 ? (int)floorf(lo / 5) * 5 : 15;
  int b = ((int)floorf(hi / 5) + 2) * 5;
  return {a, b};
}
