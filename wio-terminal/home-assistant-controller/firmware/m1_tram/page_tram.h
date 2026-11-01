// Tram page: two matching rows (next M1 from Kuckhoffstr., last M1 departed),
// then the minutes-between-trams chart at the bottom.
// Redraws a region only when its data changed (TramState.dirty) or, for the
// once-a-second texts (clock, countdown, "ago"), when the text changed.
#pragma once
#include <TFT_eSPI.h>
#include "tram_model.h"
#include "chart.h"
#include "ha.h"
#include "battery.h"

// Colors (RGB565), dark theme close to Home Assistant's.
#define C_BG     0x1082  // #121212
#define C_TEXT   0xFFFF
#define C_DIM    0x9CF3  // #9e9e9e
#define C_RULE   0x3186  // #333333
#define C_GRID   0x2124  // #252525
#define C_LINE   0x4B7C  // #4a6fe3, HA chart blue
#define C_ORANGE 0xFD20
#define C_GREEN  0x07E0
#define C_YELLOW 0xFFE0
#define C_RED    0xF800

// Layout (320x240, rotation 3).
#define Y_HEAD   0
#define H_HEAD   24
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
  char clock[8] = "";
  char countdown[16] = "";
  char ago[20] = "";
  HaStatus status = (HaStatus)255;
  uint32_t chartSlot = 0;  // now / 300 at the last chart draw: the time axis moves
};
static TramView tv;

static void drawHeader(TFT_eSPI& tft, uint32_t now, HaStatus st, bool full) {
  char clk[8];
  formatClock(now, clk, sizeof(clk));
  bool bat = batteryPoll();
  if (!full && !bat && st == tv.status && !strcmp(clk, tv.clock)) return;
  tft.fillRect(0, Y_HEAD, 320, H_HEAD, C_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(C_TEXT);
  tft.setTextDatum(L_BASELINE);
  batteryDraw(tft, 6, 6, C_BG, C_TEXT);  // top-left corner
  tft.drawString("M1 Tram", batteryOk() ? 38 : 6, 17);
  uint16_t dot = st == HA_CONNECTED ? C_GREEN : st == HA_CONNECTING ? C_YELLOW : C_RED;
  const char* note = st == HA_CONNECTING ? "Connecting" : st == HA_OFFLINE ? (netUp() ? "Offline" : "No WiFi")
                   : st == HA_AUTH_REJECTED ? "Token rejected" : "";
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(R_BASELINE);
  tft.drawString(clk, 314, 17);
  int clkW = tft.textWidth(clk);
  tft.fillCircle(314 - clkW - 10, 12, 4, dot);
  if (*note) {
    tft.setTextColor(dot);
    tft.drawString(note, 314 - clkW - 20, 17);
  }
  tft.drawFastHLine(0, Y_HEAD + H_HEAD, 320, C_RULE);
  strcpy(tv.clock, clk);
  tv.status = st;
}

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
  drawHeader(tft, now, st, full);
  drawNext(tft, s, now, full);
  drawLast(tft, s, now, full);
  drawChart(tft, s, now, full);
  s.dirty = 0;
}
