// Gaming PC page, MSI Afterburner style: one row per metric with its label
// (green GPU, blue CPU), a 15 min graph and the current value. "PC offline"
// with the last-seen time when the sensors are unavailable or stale.
#pragma once
#include <TFT_eSPI.h>
#include "gaming_model.h"
#include "header.h"
#include "timeutil.h"

#define GP_Y0     27
#define GP_ROW_H  21
#define GP_GX     76   // graph
#define GP_GW     110
#define GP_GH     15
#define GP_VAL_R  284  // right edge of the value
#define GP_UNIT_X 289

struct GamingView { int8_t online = -1; };
static GamingView gpv;

inline uint16_t gpGroup565(GpGroup g) {
  return g == GP_GRP_GPU ? hexTo565("#3fbf5f") : g == GP_GRP_CPU ? hexTo565("#4a90e2") : C_TEXT;
}

static void drawGpRow(TFT_eSPI& tft, const GamingState& G, int i) {
  const GpMetric& m = GP_METRICS[i];
  int y = GP_Y0 + i * GP_ROW_H;
  uint16_t col = gpGroup565(m.group);
  tft.fillRect(0, y, 320, GP_ROW_H, C_BG);
  tft.setTextFont(2);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(col);
  tft.drawString(m.label, 6, y + GP_ROW_H / 2);

  // Graph: last 15 min, oldest on the left.
  int gy = y + (GP_ROW_H - GP_GH) / 2;
  tft.fillRect(GP_GX, gy, GP_GW, GP_GH, C_GRID);
  float pts[GP_BUCKETS], lo, hi;
  if (gpPoints(G, i, pts)) {
    gpScale(G, i, lo, hi);
    auto py = [&](float v) {
      float f = hi > lo ? (v - lo) / (hi - lo) : 0;
      f = f < 0 ? 0 : f > 1 ? 1 : f;
      return gy + GP_GH - 1 - (int)lroundf(f * (GP_GH - 1));
    };
    for (int k = 0; k + 1 < GP_BUCKETS; k++) {
      if (isnan(pts[k]) || isnan(pts[k + 1])) continue;
      int x1 = GP_GX + k * (GP_GW - 1) / (GP_BUCKETS - 1), x2 = GP_GX + (k + 1) * (GP_GW - 1) / (GP_BUCKETS - 1);
      tft.drawLine(x1, py(pts[k]), x2, py(pts[k + 1]), col);
    }
  }

  if (i == GP_GPU_TEMP && G.hot.known) {  // hot spot next to the GPU temperature
    char h[12];
    snprintf(h, sizeof(h), "hot %.0f", (double)G.hot.v);
    tft.setTextFont(1);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_DIM);
    tft.drawString(h, GP_GX + GP_GW + 6, y + GP_ROW_H / 2);
  }

  char v[12];
  gpFormat(G, i, v, sizeof(v));
  bool known = strcmp(v, "--") != 0;
  uint16_t warn = gpValue565(G, i);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(R_BASELINE);
  tft.setTextColor(!known ? C_DIM : warn ? warn : C_TEXT);
  tft.drawString(v, GP_VAL_R, y + 16);
  tft.setTextFont(2);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_DIM);
  int ux = GP_UNIT_X;
  const char* u = m.unit;
  if (u[0] == 'o') { tft.drawCircle(ux + 2, y + 7, 2, C_DIM); ux += 6; u++; }
  tft.drawString(u, ux, y + GP_ROW_H / 2);
}

static void drawGpOffline(TFT_eSPI& tft, const GamingState& G, uint32_t now) {
  tft.fillRect(0, GP_Y0, 320, 240 - GP_Y0, C_BG);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_DIM);
  tft.drawString("PC offline", 160, 110);
  char line[40] = "no data yet";
  if (G.lastSeen) {
    char clk[8];
    formatClock(G.lastSeen, clk, sizeof(clk));
    if (now > G.lastSeen + 86400) snprintf(line, sizeof(line), "last seen over a day ago");
    else snprintf(line, sizeof(line), "last seen %s", clk);
  }
  tft.setFreeFont(&FreeSans9pt7b);
  tft.drawString(line, 160, 142);
}

static void gamingDraw(TFT_eSPI& tft, GamingState& G, uint32_t now, HaStatus st, bool full) {
  if (full) tft.fillScreen(C_BG);
  drawHeader(tft, "Gaming PC", now, st, full);
  if (now) gpAdvance(G, now / GP_BUCKET_S);  // graphs move on every 10 s
  int8_t online = gpOnline(G, now);
  if (online != gpv.online) { full = true; gpv.online = online; }
  if (!online) {
    if (full) drawGpOffline(tft, G, now);
  } else {
    for (int i = 0; i < GP_COUNT; i++)
      if (full || (G.dirty & (1u << i))) drawGpRow(tft, G, i);
  }
  G.dirty = 0;
}
