// Gaming PC page state, MSI Afterburner style: ten metrics of the "Gaming PC"
// dashboard (sensors from the PC, every ~10 s), each with its last 15 min as
// 10 s buckets for a small graph. The 15 min are loaded per sensor after
// each connect (a few KB each); live changes extend them.
// Warning colours follow the dashboard's chips.
// Pure logic on ArduinoJson; host-tested (test/run.sh).
#pragma once
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "climate_model.h"  // Reading, climateSet
#include "chart.h"          // GapPoint, histPoint, hexTo565

#define GP_PFX "sensor.desktop_a498ue6_"
#define GP_HOTSPOT_ID GP_PFX "amd_radeon_rx_6750_xt_gpu_hot_spot_temperature"

enum GpGroup : uint8_t { GP_GRP_FPS, GP_GRP_GPU, GP_GRP_CPU };
struct GpMetric {
  const char* label; const char* id; const char* unit;
  GpGroup group;
  float div;      // shown value = state / div (VRAM MB -> GB)
  uint8_t dec;    // decimals shown
  float lo, hiMin;  // graph range: lo .. max(hiMin, highest value in the window)
};
enum : uint8_t { GP_FPS, GP_GPU_LOAD, GP_GPU_TEMP, GP_GPU_CLOCK, GP_GPU_POWER, GP_VRAM,
                 GP_CPU_LOAD, GP_CPU_TEMP, GP_CPU_CLOCK, GP_RAM, GP_COUNT };
static const GpMetric GP_METRICS[GP_COUNT] = {
  {"FPS",       GP_PFX "amd_radeon_rx_6750_xt_fullscreen_fps_factor",       "fps", GP_GRP_FPS, 1,    0, 0,  60},
  {"GPU load",  GP_PFX "amd_radeon_rx_6750_xt_gpu_core_load",               "%",   GP_GRP_GPU, 1,    0, 0,  100},
  {"GPU temp",  GP_PFX "amd_radeon_rx_6750_xt_gpu_core_temperature",        "oC",  GP_GRP_GPU, 1,    0, 20, 100},
  {"GPU clock", GP_PFX "amd_radeon_rx_6750_xt_gpu_core_clock",              "MHz", GP_GRP_GPU, 1,    0, 0,  1000},
  {"GPU power", GP_PFX "amd_radeon_rx_6750_xt_gpu_package_power",           "W",   GP_GRP_GPU, 1,    0, 0,  50},
  {"VRAM",      GP_PFX "amd_radeon_rx_6750_xt_gpu_memory_used_data",        "GB",  GP_GRP_GPU, 1024, 1, 0,  12288},
  {"CPU load",  GP_PFX "amd_ryzen_7_5800x3d_cpu_total_load",                "%",   GP_GRP_CPU, 1,    0, 0,  100},
  {"CPU temp",  GP_PFX "amd_ryzen_7_5800x3d_core_tctl_tdie_temperature",    "oC",  GP_GRP_CPU, 1,    0, 20, 100},
  {"CPU clock", GP_PFX "amd_ryzen_7_5800x3d_cores_average_clock",           "MHz", GP_GRP_CPU, 1,    0, 0,  1000},
  {"RAM",       GP_PFX "total_memory_memory_load",                          "%",   GP_GRP_CPU, 1,    0, 0,  100},
};
#define GP_BUCKET_S 10
#define GP_BUCKETS  90   // 15 min
#define GP_WINDOW_S (GP_BUCKETS * GP_BUCKET_S)
#define GP_STALE_S  180  // no change from any sensor for this long: the PC is off

struct GpSeries { float sum[GP_BUCKETS]; uint16_t n[GP_BUCKETS]; };

struct GamingState {
  Reading v[GP_COUNT], hot;
  GpSeries s[GP_COUNT];
  uint32_t head;      // newest bucket number (t / GP_BUCKET_S)
  uint32_t fillTo;    // history loaded up to here; 0 = not started
  uint8_t fillNext;   // next sensor to load; GP_COUNT = done
  uint32_t lastSeen;  // newest time any sensor reported a value
  uint16_t dirty;     // one bit per row
};

inline bool gpOnline(const GamingState& G, uint32_t now) {
  bool any = false;
  for (int i = 0; i < GP_COUNT; i++) any = any || G.v[i].known;
  if (!any) return false;
  return !now || now < G.lastSeen || now - G.lastSeen <= GP_STALE_S;
}

inline void gpFormat(const GamingState& G, int i, char* buf, size_t n) {
  Reading r = G.v[i];
  if (!r.known || (i == GP_FPS && r.v <= 0)) { snprintf(buf, n, "--"); return; }  // FPS: 0/-1 outside fullscreen
  snprintf(buf, n, "%.*f", GP_METRICS[i].dec, (double)(r.v / GP_METRICS[i].div));
}

// Warning colour of a row's value, as on the dashboard chips; 0 = normal.
inline uint16_t gpValue565(const GamingState& G, int i) {
  const uint16_t amber = hexTo565("#c98a0b"), red = hexTo565("#d1453b");
  Reading r = i == GP_GPU_TEMP ? G.hot : G.v[i];  // the GPU chip goes by its hot spot
  if (!r.known) return 0;
  switch (i) {
    case GP_CPU_TEMP: return r.v > 85 ? red : r.v > 75 ? amber : 0;
    case GP_GPU_TEMP: return r.v > 100 ? red : r.v > 90 ? amber : 0;
    case GP_RAM:      return r.v > 90 ? red : r.v > 75 ? amber : 0;
    case GP_FPS:      return r.v <= 0 ? 0 : r.v < 30 ? red : r.v < 60 ? amber : hexTo565("#3fbf5f");
    default:          return 0;
  }
}

inline void gpAdvance(GamingState& G, uint32_t bucket) {
  if (bucket <= G.head) return;
  uint32_t steps = bucket - G.head;
  if (steps > GP_BUCKETS) steps = GP_BUCKETS;
  for (uint32_t k = 0; k < steps; k++) {
    uint32_t slot = (bucket - k) % GP_BUCKETS;
    for (int i = 0; i < GP_COUNT; i++) { G.s[i].sum[slot] = 0; G.s[i].n[slot] = 0; }
  }
  G.head = bucket;
  G.dirty = 0xFFFF >> (16 - GP_COUNT);  // every graph moves
}

inline void gpAddSample(GamingState& G, int i, uint32_t t, float v) {
  uint32_t b = t / GP_BUCKET_S;
  gpAdvance(G, b);
  if (G.head - b >= GP_BUCKETS) return;
  G.s[i].sum[b % GP_BUCKETS] += v;
  G.s[i].n[b % GP_BUCKETS]++;
  G.dirty |= (uint16_t)(1u << i);
}

inline void gpBeginFill(GamingState& G, uint32_t now) {
  memset(G.s, 0, sizeof(G.s));
  G.head = now / GP_BUCKET_S;
  G.fillTo = now;
  G.fillNext = 0;
  G.dirty = 0xFFFF >> (16 - GP_COUNT);
}

inline bool gpNextRequest(const GamingState& G, uint8_t& metric, uint32_t& start, uint32_t& end) {
  if (!G.fillTo || G.fillNext >= GP_COUNT) return false;
  metric = G.fillNext;
  end = G.fillTo;
  start = (G.head - (GP_BUCKETS - 1)) * GP_BUCKET_S;  // oldest bucket: the state at start_time stays in the graph
  return true;
}

inline void gpFillAdvance(GamingState& G) {
  if (G.fillNext < GP_COUNT) G.fillNext++;
}

inline void gamingApplyHistory(GamingState& G, JsonVariantConst result) {
  for (int i = 0; i < GP_COUNT; i++) {
    GapPoint g;
    for (JsonObjectConst p : result[GP_METRICS[i].id].as<JsonArrayConst>()) {
      if (!histPoint(p, g) || g.t >= G.fillTo) continue;
      gpAddSample(G, i, g.t, g.v);
      if (g.t > G.lastSeen) G.lastSeen = g.t;
    }
  }
}

inline void gamingApplyEntity(GamingState& G, const char* id, JsonObjectConst st) {
  if (!st["s"].is<const char*>() || strncmp(id, GP_PFX, sizeof(GP_PFX) - 1)) return;
  const char* s = st["s"];
  double t = st["lc"] | (st["lu"] | 0.0);
  Reading* r = nullptr;
  int row = -1;
  if (!strcmp(id, GP_HOTSPOT_ID)) { r = &G.hot; row = GP_GPU_TEMP; }
  for (int i = 0; i < GP_COUNT && !r; i++)
    if (!strcmp(id, GP_METRICS[i].id)) { r = &G.v[i]; row = i; }
  if (!r) return;
  climateSet(*r, s);
  G.dirty |= (uint16_t)(1u << row);
  if (!r->known) return;
  if (t > G.lastSeen) G.lastSeen = (uint32_t)t;
  if (r != &G.hot && G.fillTo && t >= G.fillTo) gpAddSample(G, row, (uint32_t)t, r->v);
}

inline void gamingApplyEvent(GamingState& G, JsonVariantConst event) {
  for (JsonPairConst kv : event["a"].as<JsonObjectConst>())
    gamingApplyEntity(G, kv.key().c_str(), kv.value().as<JsonObjectConst>());
  for (JsonPairConst kv : event["c"].as<JsonObjectConst>())
    gamingApplyEntity(G, kv.key().c_str(), kv.value()["+"].as<JsonObjectConst>());
}

// Metric i per bucket, oldest first, ending at `head`; empty buckets keep the
// previous value, NAN before the first. Returns how many are known.
inline int gpPoints(const GamingState& G, int i, float* out) {
  float last = NAN;
  int known = 0;
  for (int k = 0; k < GP_BUCKETS; k++) {
    uint32_t slot = (G.head - (GP_BUCKETS - 1) + k) % GP_BUCKETS;
    if (G.head >= (uint32_t)(GP_BUCKETS - 1 - k) && G.s[i].n[slot]) last = G.s[i].sum[slot] / G.s[i].n[slot];
    out[k] = last;
    if (!isnan(last)) known++;
  }
  return known;
}

inline void gpScale(const GamingState& G, int i, float& lo, float& hi) {
  lo = GP_METRICS[i].lo;
  hi = GP_METRICS[i].hiMin;
  for (int k = 0; k < GP_BUCKETS; k++)
    if (G.s[i].n[k]) { float v = G.s[i].sum[k] / G.s[i].n[k]; if (v > hi) hi = v; }
}
