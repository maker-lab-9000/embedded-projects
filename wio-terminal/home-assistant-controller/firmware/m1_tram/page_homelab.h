// Homelab page: Proxmox host CPU and RAM (value and bar, coloured by the
// card's thresholds), then the 24 h temperature chart with a legend that
// shows each sensor's current value.
#pragma once
#include <TFT_eSPI.h>
#include "homelab_model.h"
#include "header.h"

#define HL_USE_Y   26
#define HL_USE_H   38
#define HL_PLOT_X  24
#define HL_PLOT_Y  76
#define HL_PLOT_W  290
#define HL_PLOT_H  112
#define HL_LEG_Y   204

struct HomelabView { uint32_t chartAt = 0; };
static HomelabView hlv;

static void drawUsage(TFT_eSPI& tft, int x, const char* label, Reading r) {
  int y = HL_USE_Y, w = 140;
  tft.fillRect(x, y, w + 10, HL_USE_H, C_BG);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(L_BASELINE);
  tft.setTextColor(C_DIM);
  tft.drawString(label, x, y + 18);
  char v[12];
  climateFormat(r, 0, v, sizeof(v));
  tft.setTextDatum(R_BASELINE);
  tft.setTextColor(C_DIM);
  tft.drawString("%", x + w, y + 18);
  int pw = tft.textWidth("%");
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(r.known ? C_TEXT : C_DIM);
  tft.drawString(v, x + w - pw - 3, y + 18);
  tft.fillRect(x, y + 26, w, 6, C_GRID);
  if (r.known) {
    float f = r.v < 0 ? 0 : r.v > 100 ? 1 : r.v / 100;
    int bw = (int)(f * w + 0.5f);
    if (bw) tft.fillRect(x, y + 26, bw, 6, hlUsage565(r));
  }
}

static void drawTempChart(TFT_eSPI& tft, const HomelabState& H) {
  tft.fillRect(0, HL_PLOT_Y - 8, 320, HL_PLOT_H + 24, C_BG);
  tft.setTextFont(1);
  tft.setTextColor(C_DIM);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Temperatures, 24 h", 6, HL_PLOT_Y - 8 - 0);
  ChartScale c = hlScale(H);
  int step = c.yMax - c.yMin > 50 ? 20 : 10;
  tft.setTextDatum(MR_DATUM);
  for (int v = (c.yMin + step - 1) / step * step; v <= c.yMax; v += step) {
    int y = chartY(v, c, HL_PLOT_Y, HL_PLOT_H);
    tft.drawFastHLine(HL_PLOT_X, y, HL_PLOT_W, C_GRID);
    char l[6];
    snprintf(l, sizeof(l), "%d", v);
    tft.drawString(l, HL_PLOT_X - 4, y);
  }
  tft.drawFastHLine(HL_PLOT_X, HL_PLOT_Y + HL_PLOT_H, HL_PLOT_W, C_RULE);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("-24h", HL_PLOT_X, HL_PLOT_Y + HL_PLOT_H + 4);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("-12h", HL_PLOT_X + HL_PLOT_W / 2, HL_PLOT_Y + HL_PLOT_H + 4);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("now", HL_PLOT_X + HL_PLOT_W, HL_PLOT_Y + HL_PLOT_H + 4);
  float pts[HL_BUCKETS];
  for (int i = HL_TEMP_COUNT - 1; i >= 0; i--) {  // CPU drawn last, on top
    if (!hlPoints(H, i, pts)) continue;
    uint16_t col = hexTo565(HL_TEMPS[i].colour);
    for (int k = 0; k + 1 < HL_BUCKETS; k++) {
      if (isnan(pts[k]) || isnan(pts[k + 1])) continue;
      int x1 = HL_PLOT_X + k * HL_PLOT_W / (HL_BUCKETS - 1);
      int x2 = HL_PLOT_X + (k + 1) * HL_PLOT_W / (HL_BUCKETS - 1);
      int y1 = chartY(pts[k], c, HL_PLOT_Y, HL_PLOT_H), y2 = chartY(pts[k + 1], c, HL_PLOT_Y, HL_PLOT_H);
      tft.drawLine(x1, y1, x2, y2, col);
      tft.drawLine(x1, y1 + 1, x2, y2 + 1, col);  // 2 px, as the card's line_width
    }
  }
}

static void drawTempLegend(TFT_eSPI& tft, const HomelabState& H) {
  tft.fillRect(0, HL_LEG_Y, 320, 240 - HL_LEG_Y, C_BG);
  for (int i = 0; i < HL_TEMP_COUNT; i++) {
    int x = 4 + i * 63;
    uint16_t col = hexTo565(HL_TEMPS[i].colour);
    tft.setTextFont(1);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(col);
    tft.drawString(HL_TEMPS[i].name, x, HL_LEG_Y + 2);
    char v[12];
    climateFormat(H.temp[i], 0, v, sizeof(v));
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextDatum(L_BASELINE);
    tft.setTextColor(H.temp[i].known ? C_TEXT : C_DIM);
    tft.drawString(v, x, HL_LEG_Y + 30);
    int w = tft.textWidth(v);
    tft.drawCircle(x + w + 4, HL_LEG_Y + 19, 2, C_DIM);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(C_DIM);
    tft.drawString("C", x + w + 8, HL_LEG_Y + 30);
  }
}

static void homelabDraw(TFT_eSPI& tft, HomelabState& H, uint32_t now, HaStatus st, bool full) {
  if (full) tft.fillScreen(C_BG);
  drawHeader(tft, "Homelab", now, st, full);
  if (full || (H.dirty & HL_DIRTY_CPU)) drawUsage(tft, 8, "Host CPU", H.cpu);
  if (full || (H.dirty & HL_DIRTY_RAM)) drawUsage(tft, 168, "Host RAM", H.ram);
  if (full) tft.drawFastHLine(0, HL_USE_Y + HL_USE_H + 1, 320, C_RULE);
  if (now) hlAdvance(H, now / HL_BUCKET_S);  // the time axis moves on even without changes
  // The chart changes with every sample; redraw it at most every 5 s.
  bool chart = full || ((H.dirty & HL_DIRTY_CHART) && (now - hlv.chartAt >= 5 || now < hlv.chartAt));
  if (chart) {
    drawTempChart(tft, H);
    hlv.chartAt = now;
    H.dirty &= ~HL_DIRTY_CHART;
  }
  if (full || (H.dirty & HL_DIRTY_TEMPS)) drawTempLegend(tft, H);
  H.dirty &= HL_DIRTY_CHART;  // keep a skipped chart redraw pending
}
