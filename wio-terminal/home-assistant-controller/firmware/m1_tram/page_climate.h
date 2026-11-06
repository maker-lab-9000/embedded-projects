// Climate page: one row per room (temperature with a coloured thermometer,
// humidity with a coloured droplet), then PM2.5 and PM10. Colours follow the
// dashboard's rules (climate_model.h). Redraws a row only when it changed.
#pragma once
#include <TFT_eSPI.h>
#include "climate_model.h"
#include "header.h"

#define C_BLUE 0x4C9C  // #4a90e2

#define CL_Y0     26
#define CL_ROW_H  33
#define CL_PM_Y0  (CL_Y0 + CLIMATE_ROOM_COUNT * CL_ROW_H + 4)
#define CL_TEMP_R 196  // right edge of the temperature text
#define CL_DROP_X 214
#define CL_TEXT_DY 22  // baseline offset inside a row

inline uint16_t climate565(ClimateColour c) {
  switch (c) {
    case CL_BLUE: return C_BLUE;
    case CL_GREEN: return C_GREEN;
    case CL_YELLOW: return C_YELLOW;
    case CL_RED: return C_RED;
    default: return C_DIM;
  }
}

static void drawThermometer(TFT_eSPI& tft, int x, int y, uint16_t col) {
  tft.fillRoundRect(x + 2, y, 6, 14, 3, col);
  tft.fillCircle(x + 5, y + 15, 5, col);
}

static void drawDroplet(TFT_eSPI& tft, int x, int y, uint16_t col) {
  tft.fillTriangle(x + 6, y, x + 1, y + 11, x + 11, y + 11, col);
  tft.fillCircle(x + 6, y + 13, 5, col);
}

static void drawParticles(TFT_eSPI& tft, int x, int y, uint16_t col) {
  tft.fillCircle(x + 3, y + 4, 2, col);
  tft.fillCircle(x + 9, y + 7, 3, col);
  tft.fillCircle(x + 3, y + 13, 3, col);
  tft.fillCircle(x + 10, y + 16, 2, col);
}

// Draws "<value> <unit>" right-aligned at `right`; the value bold, the unit
// dim. A unit starting with 'o' gets a drawn degree sign (fonts are ASCII).
static void drawValue(TFT_eSPI& tft, int right, int base, const char* value, const char* unit, bool known) {
  tft.setTextDatum(R_BASELINE);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(C_DIM);
  bool degree = unit[0] == 'o';
  const char* u = degree ? unit + 1 : unit;
  tft.drawString(u, right, base);
  int x = right - tft.textWidth(u);
  if (degree) {
    tft.drawCircle(x - 5, base - 11, 2, C_DIM);
    x -= 9;
  }
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(known ? C_TEXT : C_DIM);
  tft.drawString(value, x - 3, base);
}

static void drawRoom(TFT_eSPI& tft, const ClimateState& c, int i) {
  int y = CL_Y0 + i * CL_ROW_H, base = y + CL_TEXT_DY;
  tft.fillRect(0, y, 320, CL_ROW_H, C_BG);
  drawThermometer(tft, 8, y + 6, climate565(tempColour(c.temp[i])));
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT);
  tft.setTextDatum(L_BASELINE);
  tft.drawString(CLIMATE_ROOMS[i].name, 30, base);
  char v[12];
  climateFormat(c.temp[i], 1, v, sizeof(v));
  drawValue(tft, CL_TEMP_R, base, v, "oC", c.temp[i].known);
  drawDroplet(tft, CL_DROP_X, y + 6, climate565(humColour(c.hum[i])));
  climateFormat(c.hum[i], 0, v, sizeof(v));
  drawValue(tft, 314, base, v, "%", c.hum[i].known);
}

static void drawPm(TFT_eSPI& tft, const ClimateState& c, int i) {
  int y = CL_PM_Y0 + i * CL_ROW_H, base = y + CL_TEXT_DY;
  tft.fillRect(0, y, 320, CL_ROW_H, C_BG);
  drawParticles(tft, 8, y + 6, climate565(pmColour(c.pm[i], CLIMATE_PM[i].limit)));
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT);
  tft.setTextDatum(L_BASELINE);
  tft.drawString(CLIMATE_PM[i].name, 30, base);
  char lim[12];
  snprintf(lim, sizeof(lim), "< %d", (int)CLIMATE_PM[i].limit);
  tft.setTextColor(C_DIM);
  tft.drawString(lim, 30 + tft.textWidth(CLIMATE_PM[i].name) + 8, base);
  char v[12];
  climateFormat(c.pm[i], 1, v, sizeof(v));
  drawValue(tft, 314, base, v, "ug/m3", c.pm[i].known);
}

// Draws the page; `full` repaints everything (page switch, wake from sleep).
static void climateDraw(TFT_eSPI& tft, ClimateState& c, uint32_t now, HaStatus st, bool full) {
  if (full) tft.fillScreen(C_BG);
  drawHeader(tft, "Climate", now, st, full);
  for (int i = 0; i < CLIMATE_ROOM_COUNT; i++)
    if (full || (c.dirty & CLIMATE_DIRTY_ROOM(i))) drawRoom(tft, c, i);
  if (full) tft.drawFastHLine(0, CL_PM_Y0 - 3, 320, C_RULE);
  for (int i = 0; i < CLIMATE_PM_COUNT; i++)
    if (full || (c.dirty & CLIMATE_DIRTY_PM(i))) drawPm(tft, c, i);
  c.dirty = 0;
}
