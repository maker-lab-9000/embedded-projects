// Tram page: next M1 from Kuckhoffstr., minutes-between-trams chart, last departure.
// Redraws a region only when its data changed (TramState.dirty) or, for the
// once-a-second texts (clock, countdown, "ago"), when the text changed.
#pragma once
#include <TFT_eSPI.h>
#include "tram_model.h"
#include "chart.h"
#include "ha.h"

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
#define Y_NEXT   25
#define H_NEXT   74
#define Y_CHART  101
#define H_CHART  108
#define Y_LAST   211
#define H_LAST   29
#define PLOT_X   30
#define PLOT_W   282
#define PLOT_Y   128
#define PLOT_H   60

struct TramView {
  char clock[8] = "";
  char countdown[16] = "";
  char ago[20] = "";
  HaStatus status = (HaStatus)255;
  uint32_t chartSlot = 0;  // now / 300 at the last chart draw: the time axis moves
};
static TramView tv;

// Shortens `s` with ".." until it fits `maxW` pixels in the current font.
static void fitText(TFT_eSPI& tft, char* s, int maxW) {
  size_t n = strlen(s);
  if (tft.textWidth(s) <= maxW) return;
  while (n > 2) {
    s[--n] = 0;
    s[n - 1] = '.'; s[n - 2] = '.';
    if (tft.textWidth(s) <= maxW) return;
  }
}

static void drawHeader(TFT_eSPI& tft, uint32_t now, HaStatus st, bool full) {
  char clk[8];
  formatClock(now, clk, sizeof(clk));
  if (!full && st == tv.status && !strcmp(clk, tv.clock)) return;
  tft.fillRect(0, Y_HEAD, 320, H_HEAD, C_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(C_TEXT);
  tft.setTextDatum(L_BASELINE);
  tft.drawString("Kuckhoffstr.", 6, 17);
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

static void drawNext(TFT_eSPI& tft, TramState& s, uint32_t now, bool full) {
  int first = tramFirstUpcoming(s, now);  // skip trams that already left
  char cd[16] = "";
  if (first >= 0) formatCountdown(now, s.dep[first].epoch, cd, sizeof(cd));
  if (!full && !(s.dirty & DIRTY_NEXT) && !strcmp(cd, tv.countdown)) return;
  strcpy(tv.countdown, cd);
  tft.fillRect(0, Y_NEXT, 320, H_NEXT, C_BG);
  tft.setTextDatum(MC_DATUM);
  if (!s.depKnown || first < 0) {
    tft.setFreeFont(&FreeSans12pt7b);
    tft.setTextColor(C_DIM);
    tft.drawString(s.depKnown ? "No trams scheduled" : "Waiting for data...", 160, Y_NEXT + H_NEXT / 2);
    return;
  }
  const Departure& d = s.dep[first];
  // Row 1: line badge in the line's own color, big countdown on the right
  // (orange when late, with the delay just left of it).
  tft.fillRoundRect(6, Y_NEXT + 6, 46, 30, 4, d.color565);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(C_TEXT);
  tft.drawString(d.line, 29, Y_NEXT + 21);
  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.setTextDatum(R_BASELINE);
  tft.setTextColor(d.delayMin > 0 ? C_ORANGE : C_TEXT);
  tft.drawString(cd, 314, Y_NEXT + 34);
  if (d.delayMin > 0) {
    char dl[8];
    snprintf(dl, sizeof(dl), "+%d", d.delayMin);
    int cdW = tft.textWidth(cd);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.drawString(dl, 314 - cdW - 8, Y_NEXT + 34);
  }
  // Row 2: direction, full width.
  char dir[44];
  snprintf(dir, sizeof(dir), "> %s", d.dir);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT);
  tft.setTextDatum(L_BASELINE);
  fitText(tft, dir, 308);
  tft.drawString(dir, 6, Y_NEXT + 54);
  // Row 3: following departures.
  int more = s.depCount - first - 1;
  if (more > 0) {
    char then[40];
    if (more > 1) snprintf(then, sizeof(then), "then %s, %s", s.dep[first + 1].time, s.dep[first + 2].time);
    else snprintf(then, sizeof(then), "then %s", s.dep[first + 1].time);
    tft.setTextColor(C_DIM);
    tft.drawString(then, 6, Y_NEXT + 71);
  }
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

static void drawTramIcon(TFT_eSPI& tft, int x, int y) {
  tft.fillRoundRect(x, y, 14, 15, 3, C_RED);
  tft.fillRect(x + 3, y + 3, 8, 5, C_BG);
  tft.drawFastHLine(x + 4, y - 3, 6, C_RED);
  tft.drawLine(x + 2, y + 15, x, y + 18, C_RED);
  tft.drawLine(x + 11, y + 15, x + 13, y + 18, C_RED);
}

static void drawLast(TFT_eSPI& tft, TramState& s, uint32_t now, bool full) {
  char ago[20];
  formatAgo(now, s.lastDeparted, ago, sizeof(ago));
  if (!full && !(s.dirty & DIRTY_LAST) && !strcmp(ago, tv.ago)) return;
  tft.fillRect(0, Y_LAST, 320, H_LAST, C_BG);
  if (full) tft.drawFastHLine(0, Y_LAST - 2, 320, C_RULE);
  drawTramIcon(tft, 8, Y_LAST + 5);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT);
  tft.setTextDatum(L_BASELINE);
  tft.drawString("Last M1 departed", 32, Y_LAST + 20);
  tft.setTextDatum(R_BASELINE);
  tft.drawString(ago, 314, Y_LAST + 20);
  strcpy(tv.ago, ago);
}

// Draws the page; `full` repaints everything (page switch, wake from sleep).
static void tramDraw(TFT_eSPI& tft, TramState& s, uint32_t now, HaStatus st, bool full) {
  if (full) tft.fillScreen(C_BG);
  drawHeader(tft, now, st, full);
  drawNext(tft, s, now, full);
  drawChart(tft, s, now, full);
  drawLast(tft, s, now, full);
  s.dirty = 0;
}
