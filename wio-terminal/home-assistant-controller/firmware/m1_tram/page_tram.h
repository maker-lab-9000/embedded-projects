// Tram page: two matching rows (next M1 from Kuckhoffstr., last M1 departed),
// then the minutes-between-trams chart at the bottom.
// Redraws a region only when its data changed (TramState.dirty) or, for the
// once-a-second texts (clock, countdown, "ago"), when the text changed.
#pragma once
#include <TFT_eSPI.h>
#include "tram_model.h"
#include "chart.h"
#include "header.h"

// Layout (320x240, rotation 3).
#define H_ROW    30
#define Y_NEXT   26
#define Y_LAST   (Y_NEXT + H_ROW + 1)
#define Y_CHART  (Y_LAST + H_ROW + 3)
#define H_CHART  (240 - Y_CHART)
#define PLOT_X   30
#define PLOT_W   282
#define PLOT_Y   (Y_CHART + 30)
#define PLOT_H   (240 - PLOT_Y - 18)

struct TramView {
  char countdown[16] = "";
  char ago[20] = "";
  uint32_t chartSlot = 0;  // now / 300 at the last chart draw: the time axis moves
};
static TramView tv;

static void drawTramIcon(TFT_eSPI& tft, int x, int y, uint16_t color) {
  tft.fillRoundRect(x, y, 14, 15, 3, color);
  tft.fillRect(x + 3, y + 3, 8, 5, C_BG);
  tft.drawFastHLine(x + 4, y - 3, 6, color);
  tft.drawLine(x + 2, y + 15, x, y + 18, color);
  tft.drawLine(x + 11, y + 15, x + 13, y + 18, color);
}

// One status row: tram icon, label on the left, time on the right.
static void drawRow(TFT_eSPI& tft, int y, uint16_t icon, const char* label, const char* value, uint16_t valueColor) {
  tft.fillRect(0, y, 320, H_ROW, C_BG);
  drawTramIcon(tft, 8, y + 6, icon);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT);
  tft.setTextDatum(L_BASELINE);
  tft.drawString(label, 32, y + 21);
  tft.setTextColor(valueColor);
  tft.setTextDatum(R_BASELINE);
  tft.drawString(value, 314, y + 21);
}

// Next tram: "in 4 min", orange with the delay when late ("in 6 min +2").
static void drawNext(TFT_eSPI& tft, TramState& s, uint32_t now, bool full) {
  int first = tramFirstUpcoming(s, now);  // skip trams that already left
  char val[24];
  uint16_t col = C_TEXT;
  if (!s.depKnown) { strcpy(val, "--"); col = C_DIM; }
  else if (first < 0) { strcpy(val, "No trams"); col = C_DIM; }
  else {
    const Departure& d = s.dep[first];
    formatCountdown(now, d.epoch, val, sizeof(val));
    if (d.delayMin > 0) {
      size_t n = strlen(val);
      snprintf(val + n, sizeof(val) - n, " +%d", d.delayMin);
      col = C_ORANGE;
    }
  }
  if (!full && !(s.dirty & DIRTY_NEXT) && !strcmp(val, tv.countdown)) return;
  strcpy(tv.countdown, val);
  drawRow(tft, Y_NEXT, C_GREEN, "Kuckhoffstr.", val, col);
}

static void drawLast(TFT_eSPI& tft, TramState& s, uint32_t now, bool full) {
  char ago[20];
  formatAgo(now, s.lastDeparted, ago, sizeof(ago));
  if (!full && !(s.dirty & DIRTY_LAST) && !strcmp(ago, tv.ago)) return;
  strcpy(tv.ago, ago);
  drawRow(tft, Y_LAST, C_RED, "Last M1 departed", ago, C_TEXT);
}

static void drawChart(TFT_eSPI& tft, TramState& s, uint32_t now, bool full) {
  if (!full && !(s.dirty & (DIRTY_CHART | DIRTY_GAP)) && now / 300 == tv.chartSlot) return;
  tv.chartSlot = now / 300;
  tft.fillRect(0, Y_CHART, 320, H_CHART, C_BG);
  tft.drawFastHLine(0, Y_CHART - 2, 320, C_RULE);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT);
  tft.setTextDatum(L_BASELINE);
  tft.drawString("Minutes between trams", 6, Y_CHART + 17);
  if (s.gapKnown) {
    char v[12];
    snprintf(v, sizeof(v), "%d min", (int)lroundf(s.gapMin));
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextDatum(R_BASELINE);
    tft.drawString(v, 314, Y_CHART + 17);
  }
  ChartScale c = chartScale(s.hist, s.histCount);
  // Grid and labels (built-in 6x8 font).
  tft.setTextFont(1);
  tft.setTextColor(C_DIM);
  tft.setTextDatum(MR_DATUM);
  int ys[3] = {c.yMax, (c.yMin + c.yMax) / 2, c.yMin};
  for (int v : ys) {
    int y = chartY(v, c, PLOT_Y, PLOT_H);
    tft.drawFastHLine(PLOT_X, y, PLOT_W, C_GRID);
    tft.drawNumber(v, PLOT_X - 4, y);
  }
  tft.setTextDatum(TC_DATUM);
  static const char* xl[] = {"-24h", "-18h", "-12h", "-6h", "now"};
  for (int i = 0; i < 5; i++) {
    int x = PLOT_X + PLOT_W * i / 4;
    if (i == 4) tft.setTextDatum(TR_DATUM);
    tft.drawString(xl[i], i == 4 ? PLOT_X + PLOT_W : x, PLOT_Y + PLOT_H + 6);
  }
  if (!s.histCount || !now) {
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.drawString(s.histKnown ? "No data" : "Loading...", PLOT_X + PLOT_W / 2, PLOT_Y + PLOT_H / 2);
    return;
  }
  // Step line, 2 px thick, extended to "now" at the last value.
  for (int i = 0; i < s.histCount; i++) {
    int x1 = chartX(s.hist[i].t, now, PLOT_X, PLOT_W);
    int x2 = i + 1 < s.histCount ? chartX(s.hist[i + 1].t, now, PLOT_X, PLOT_W) : PLOT_X + PLOT_W;
    int y1 = chartY(s.hist[i].v, c, PLOT_Y, PLOT_H);
    tft.drawFastHLine(x1, y1, x2 - x1 + 1, C_LINE);
    tft.drawFastHLine(x1, y1 - 1, x2 - x1 + 1, C_LINE);
    if (i + 1 < s.histCount) {
      int y2 = chartY(s.hist[i + 1].v, c, PLOT_Y, PLOT_H);
      int top = y1 < y2 ? y1 : y2, h = abs(y2 - y1) + 1;
      tft.drawFastVLine(x2, top - 1, h, C_LINE);
      tft.drawFastVLine(x2 + 1, top - 1, h, C_LINE);
    }
  }
}

// Draws the page; `full` repaints everything (page switch, wake from sleep).
static void tramDraw(TFT_eSPI& tft, TramState& s, uint32_t now, HaStatus st, bool full) {
  if (full) tft.fillScreen(C_BG);
  drawHeader(tft, "M1 Tram", now, st, full);
  drawNext(tft, s, now, full);
  drawLast(tft, s, now, full);
  drawChart(tft, s, now, full);
  s.dirty = 0;
}
