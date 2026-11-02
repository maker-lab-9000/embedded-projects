// Power page: "Using now · all plugs" in large type, then one row per plug
// with its watts and a bar for its share of the total.
#pragma once
#include <TFT_eSPI.h>
#include "power_model.h"
#include "header.h"
#include "page_climate.h"  // drawValue

#define PW_TOTAL_Y 26
#define PW_TOTAL_H 42
#define PW_Y0      (PW_TOTAL_Y + PW_TOTAL_H + 2)
#define PW_ROW_H   24
#define PW_BAR_X   164
#define PW_BAR_W   76

static void drawBolt(TFT_eSPI& tft, int x, int y, uint16_t col) {
  tft.fillTriangle(x + 8, y, x, y + 12, x + 7, y + 12, col);
  tft.fillTriangle(x + 5, y + 10, x + 12, y + 10, x + 4, y + 22, col);
}

static void drawPowerTotal(TFT_eSPI& tft, const PowerState& P) {
  int y = PW_TOTAL_Y, base = y + 31;
  tft.fillRect(0, y, 320, PW_TOTAL_H, C_BG);
  drawBolt(tft, 10, y + 10, P.total.known ? C_YELLOW : C_DIM);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(L_BASELINE);
  tft.setTextColor(C_DIM);
  tft.drawString("Using now", 32, y + 18);
  tft.drawString("all plugs", 32, y + 36);
  int wW = tft.textWidth("W");
  tft.drawString("W", 314 - wW, base);
  char v[16];
  powerFormat(P.total, v, sizeof(v));
  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.setTextDatum(R_BASELINE);
  tft.setTextColor(P.total.known ? C_TEXT : C_DIM);
  tft.drawString(v, 314 - wW - 5, base);
}

static void drawPowerRow(TFT_eSPI& tft, const PowerState& P, int i) {
  int y = PW_Y0 + i * PW_ROW_H, base = y + 17;
  const Reading& r = P.dev[i];
  bool active = r.known && r.v >= 1;
  tft.fillRect(0, y, 320, PW_ROW_H, C_BG);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(L_BASELINE);
  tft.setTextColor(active ? C_TEXT : C_DIM);
  tft.drawString(POWER_DEVICES[i].name, 10, base);
  tft.fillRect(PW_BAR_X, y + 9, PW_BAR_W, 8, C_GRID);
  int w = (int)(powerShare(r, P.total) * PW_BAR_W + 0.5f);
  if (w > 0) tft.fillRect(PW_BAR_X, y + 9, w, 8, C_LINE);
  char v[16];
  powerFormat(r, v, sizeof(v));
  drawValue(tft, 314, base, v, "W", active);
}

static void powerDraw(TFT_eSPI& tft, PowerState& P, uint32_t now, HaStatus st, bool full) {
  if (full) tft.fillScreen(C_BG);
  drawHeader(tft, "Power", now, st, full);
  bool total = full || (P.dirty & POWER_DIRTY_TOTAL);
  if (total) drawPowerTotal(tft, P);
  if (full) tft.drawFastHLine(0, PW_Y0 - 2, 320, C_RULE);
  for (int i = 0; i < POWER_DEVICE_COUNT; i++)  // the total moves every bar
    if (total || (P.dirty & POWER_DIRTY_DEV(i))) drawPowerRow(tft, P, i);
  P.dirty = 0;
}
