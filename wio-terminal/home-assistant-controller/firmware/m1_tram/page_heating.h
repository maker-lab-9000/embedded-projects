// Heating page: the two thermostats (radiator icon, orange while heating;
// current temperature and target), then the selected one's settings:
// power and target temperature. Keys: heating_model.h (heatingOnKey).
#pragma once
#include <TFT_eSPI.h>
#include "heating_model.h"
#include "header.h"
#include "page_climate.h"  // drawValue (degree sign)

#define HT_Y0      26
#define HT_ROW_H   46
#define HT_PANEL_Y (HT_Y0 + THERMO_COUNT * HT_ROW_H + 6)
#define HT_FIELD_H 36
#define HT_HINT_Y  228

static void drawRadiator(TFT_eSPI& tft, int x, int y, uint16_t col) {
  for (int i = 0; i < 3; i++) tft.fillRoundRect(x + i * 6, y, 4, 18, 2, col);
  tft.fillRect(x - 1, y + 14, 18, 2, col);
}

static void drawThermoRow(TFT_eSPI& tft, const HeatingState& H, int i) {
  const ThermoState& t = H.t[i];
  int y = HT_Y0 + i * HT_ROW_H;
  bool sel = i == H.sel;
  uint16_t bg = sel ? C_SEL : C_BG;
  tft.fillRect(0, y, 320, HT_ROW_H, bg);
  if (sel && H.editing) tft.fillRect(0, y, 3, HT_ROW_H, C_LINE);
  bool on = thermoIsOn(t);
  uint16_t icon = !t.available ? C_RULE : t.heating ? C_ORANGE : on ? C_TEXT : C_DIM;
  drawRadiator(tft, 9, y + 13, icon);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(L_BASELINE);
  tft.setTextColor(t.available ? C_TEXT : C_DIM, bg);
  tft.drawString(THERMOS[i].name, 34, y + 19);
  const char* st = !t.known ? "--" : !t.available ? "Unavailable" : t.mode == HV_OFF ? "Off"
                 : t.heating ? "Heating" : t.mode == HV_AUTO ? "Auto" : "Idle";
  tft.setTextFont(2);
  tft.setTextColor(t.heating && t.available ? C_ORANGE : C_DIM, bg);
  tft.drawString(st, 34, y + 38);
  // Right: current temperature, and the target below it.
  char v[12];
  climateFormat({t.current, t.known && t.hasCurrent}, 1, v, sizeof(v));
  tft.fillRect(150, y + 2, 166, 42, bg);
  drawValue(tft, 314, y + 19, v, "oC", t.known && t.hasCurrent);
  char tg[20];
  if (t.known && t.hasTarget) snprintf(tg, sizeof(tg), "target %.1f", (double)thermoShownTarget(H, i));
  else strcpy(tg, "target --");
  tft.setTextFont(2);
  tft.setTextDatum(R_BASELINE);
  tft.setTextColor(on ? C_TEXT : C_DIM, bg);
  tft.drawString(tg, 314, y + 38);
}

static void drawHeatPanel(TFT_eSPI& tft, const HeatingState& H) {
  const ThermoState& t = H.t[H.sel];
  bool ok = t.known && t.available;
  int right = H.editing ? 302 : 314;
  for (int f = 0; f < 2; f++) {
    bool focus = H.editing && H.field == f;
    int y = HT_PANEL_Y + f * HT_FIELD_H, base = y + 23;
    uint16_t bg = focus ? C_SEL : C_BG;
    tft.fillRect(0, y, 320, HT_FIELD_H, bg);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextDatum(L_BASELINE);
    tft.setTextColor(C_DIM, bg);
    tft.drawString(f == 0 ? "Power" : "Target", 12, base);
    if (focus) {
      tft.fillTriangle(108, y + 18, 114, y + 13, 114, y + 23, C_TEXT);
      tft.fillTriangle(314, y + 18, 308, y + 13, 308, y + 23, C_TEXT);
    }
    if (f == 0) {
      tft.setTextDatum(R_BASELINE);
      tft.setTextColor(ok ? C_TEXT : C_DIM, bg);
      tft.drawString(!t.known ? "--" : !t.available ? "Unavailable" : t.mode == HV_OFF ? "Off"
                     : t.mode == HV_AUTO ? "On (auto)" : "On", right, base);
    } else {
      char v[12];
      climateFormat({thermoShownTarget(H, H.sel), ok && t.hasTarget}, 1, v, sizeof(v));
      drawValue(tft, right, base, v, "oC", ok && t.hasTarget);
    }
  }
}

static void drawHeatHint(TFT_eSPI& tft, const HeatingState& H) {
  tft.fillRect(0, HT_HINT_Y, 320, 240 - HT_HINT_Y, C_BG);
  tft.setTextFont(1);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(H.editing ? "up/down: setting  left/right: change  press: back"
                           : "up/down: room  press: settings  left/right: page", 160, HT_HINT_Y + 2);
}

static void heatingDraw(TFT_eSPI& tft, HeatingState& H, uint32_t now, HaStatus st, bool full) {
  if (full) tft.fillScreen(C_BG);
  drawHeader(tft, "Heating", now, st, full);
  bool ui = full || H.uiDirty;
  for (int i = 0; i < THERMO_COUNT; i++)
    if (ui || (H.dirty & (1u << i))) drawThermoRow(tft, H, i);
  if (full) tft.drawFastHLine(0, HT_PANEL_Y - 4, 320, C_RULE);
  if (ui || (H.dirty & (1u << H.sel))) drawHeatPanel(tft, H);
  if (ui) drawHeatHint(tft, H);
  H.dirty = 0;
  H.uiDirty = false;
}
