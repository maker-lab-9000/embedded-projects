// observation.h — the unified per-second record: GNSS time/position plus every sensor's
// latest values. Assembled once per second (assembleObservation() in the sketch) and
// written to /obs.csv. Header AND rows come from OBS_COLS below, so they cannot drift apart
// (/gps.csv keeps two hand-maintained literals; do not copy that pattern here).
#pragma once
#include <Arduino.h>

struct Observation {
  char     utc[24];          // "YYYY-MM-DDTHH:MM:SSZ", "" until the first GNSS date+time
  uint32_t uptimeS;
  bool     fixValid; double lat, lon; float altM, hdop; uint8_t used, inView; const char* fix;
  bool     tslOk; uint16_t tslFull, tslIr; const char* tslGain; uint16_t tslIntegMs; float tslLux; bool tslSat;
  bool     mlxOk; float mlxAmbC, mlxObjC, mlxDeltaC;
  bool     magOk; float bx, by, bz, bTotal, headingDeg;
  bool     bmeOk; float tempC, hum, presHpa;
  uint16_t loopMaxMs;
};

Observation obs;   // current record

typedef void (*ObsFmt)(char* out, size_t n, const Observation& o);
struct ObsCol { const char* name; ObsFmt fmt; };

// Absent sensor or invalid value -> empty field, never a fake zero.
static void fmtF(char* b, size_t n, bool ok, float v, const char* f) { if (ok && !isnan(v)) snprintf(b, n, f, v); else b[0] = 0; }
static void fmtU(char* b, size_t n, bool ok, unsigned v)             { if (ok) snprintf(b, n, "%u", v); else b[0] = 0; }

const ObsCol OBS_COLS[] = {
  {"utc",           [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%s", o.utc); }},
  {"uptime_s",      [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%lu", (unsigned long)o.uptimeS); }},
  {"lat",           [](char* b, size_t n, const Observation& o){ if (o.fixValid) snprintf(b, n, "%.6f", o.lat); else b[0] = 0; }},
  {"lon",           [](char* b, size_t n, const Observation& o){ if (o.fixValid) snprintf(b, n, "%.6f", o.lon); else b[0] = 0; }},
  {"alt_m",         [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.fixValid, o.altM, "%.1f"); }},
  {"fix",           [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%s", o.fix); }},
  {"hdop",          [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.fixValid, o.hdop, "%.1f"); }},
  {"used",          [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%u", o.used); }},
  {"in_view",       [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%u", o.inView); }},
  {"tsl_full",      [](char* b, size_t n, const Observation& o){ fmtU(b, n, o.tslOk, o.tslFull); }},
  {"tsl_ir",        [](char* b, size_t n, const Observation& o){ fmtU(b, n, o.tslOk, o.tslIr); }},
  {"tsl_vis",       [](char* b, size_t n, const Observation& o){ fmtU(b, n, o.tslOk, o.tslFull >= o.tslIr ? o.tslFull - o.tslIr : 0); }},
  {"tsl_gain",      [](char* b, size_t n, const Observation& o){ if (o.tslOk) snprintf(b, n, "%s", o.tslGain); else b[0] = 0; }},
  {"tsl_integ_ms",  [](char* b, size_t n, const Observation& o){ fmtU(b, n, o.tslOk, o.tslIntegMs); }},
  {"tsl_lux",       [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.tslOk, o.tslLux, "%.4f"); }},
  {"tsl_sat",       [](char* b, size_t n, const Observation& o){ fmtU(b, n, o.tslOk, o.tslSat ? 1 : 0); }},
  {"mlx_ambient_c", [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.mlxOk, o.mlxAmbC, "%.2f"); }},
  {"mlx_object_c",  [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.mlxOk, o.mlxObjC, "%.2f"); }},
  {"mlx_delta_c",   [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.mlxOk, o.mlxDeltaC, "%.2f"); }},
  {"mag_x_ut",      [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.magOk, o.bx, "%.2f"); }},
  {"mag_y_ut",      [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.magOk, o.by, "%.2f"); }},
  {"mag_z_ut",      [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.magOk, o.bz, "%.2f"); }},
  {"mag_total_ut",  [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.magOk, o.bTotal, "%.2f"); }},
  {"heading_deg",   [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.magOk, o.headingDeg, "%.1f"); }},
  {"temp_c",        [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.bmeOk, o.tempC, "%.2f"); }},
  {"humidity",      [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.bmeOk, o.hum, "%.1f"); }},
  {"pressure_hpa",  [](char* b, size_t n, const Observation& o){ fmtF(b, n, o.bmeOk, o.presHpa, "%.1f"); }},
  {"sensors_ok",    [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%u", (o.tslOk ? 1 : 0) | (o.mlxOk ? 2 : 0) | (o.magOk ? 4 : 0) | (o.bmeOk ? 8 : 0)); }},
  {"loop_max_ms",   [](char* b, size_t n, const Observation& o){ snprintf(b, n, "%u", o.loopMaxMs); }},
};
const int OBS_COL_COUNT = sizeof(OBS_COLS) / sizeof(OBS_COLS[0]);

// "name,name,...\n" -> out. False if it did not fit.
bool obsHeader(char* out, size_t n) {
  size_t pos = 0;
  for (int i = 0; i < OBS_COL_COUNT; i++) {
    int w = snprintf(out + pos, n - pos, "%s%s", i ? "," : "", OBS_COLS[i].name);
    if (w < 0 || (size_t)w >= n - pos) return false;
    pos += w;
  }
  return snprintf(out + pos, n - pos, "\n") < (int)(n - pos);
}

// One CSV row for o -> out. False if it did not fit.
bool obsRow(const Observation& o, char* out, size_t n) {
  size_t pos = 0;
  char f[32];
  for (int i = 0; i < OBS_COL_COUNT; i++) {
    OBS_COLS[i].fmt(f, sizeof(f), o);
    int w = snprintf(out + pos, n - pos, "%s%s", i ? "," : "", f);
    if (w < 0 || (size_t)w >= n - pos) return false;
    pos += w;
  }
  return snprintf(out + pos, n - pos, "\n") < (int)(n - pos);
}
