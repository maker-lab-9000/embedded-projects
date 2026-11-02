// Lights page: the three lamps (bulb in the lamp's colour, name, brightness),
// then the selected lamp's settings: power, brightness bar, colour preset.
// Keys: lights_model.h (lightsOnKey). Redraws only what changed.
#pragma once
#include <TFT_eSPI.h>
#include "lights_model.h"
#include "header.h"

#define LT_Y0     26
#define LT_ROW_H  32
#define LT_PANEL_Y (LT_Y0 + LIGHT_COUNT * LT_ROW_H + 5)
#define LT_FIELD_H 30
#define LT_HINT_Y (LT_PANEL_Y + 3 * LT_FIELD_H + 4)
#define LT_VAL_X  120  // left edge of bars and swatches

static void drawBulb(TFT_eSPI& tft, int x, int y, const LightState& s) {
  uint16_t sw = lightSwatch565(s);
  if (sw) tft.fillCircle(x + 7, y + 7, 7, sw);
  else tft.drawCircle(x + 7, y + 7, 7, s.available ? C_TEXT : C_RULE);
  tft.fillRect(x + 4, y + 16, 7, 4, s.available ? C_DIM : C_RULE);
}

static void drawLampRow(TFT_eSPI& tft, const LightsState& L, int i) {
  const LightState& s = L.l[i];
  int y = LT_Y0 + i * LT_ROW_H, base = y + 21;
  bool sel = i == L.sel;
  uint16_t bg = sel ? C_SEL : C_BG;
  tft.fillRect(0, y, 320, LT_ROW_H, bg);
  if (sel && L.editing) tft.fillRect(0, y, 3, LT_ROW_H, C_LINE);
  drawBulb(tft, 8, y + 6, s);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(L_BASELINE);
  tft.setTextColor(s.available ? C_TEXT : C_DIM, bg);
  tft.drawString(LIGHTS[i].name, 32, base);
  char v[16];
  if (!s.known) strcpy(v, "--");
  else if (!s.available) strcpy(v, "Unavailable");
  else if (!s.on) strcpy(v, "Off");
  else snprintf(v, sizeof(v), "%u %%", (unsigned)lightPct(s.brightness));
  tft.setTextDatum(R_BASELINE);
  tft.setTextColor(s.available && s.on ? C_TEXT : C_DIM, bg);
  tft.drawString(v, 314, base);
}

// One settings row; `focus` = the joystick changes this one (arrows shown).
static void drawField(TFT_eSPI& tft, int f, const char* label, bool focus) {
  int y = LT_PANEL_Y + f * LT_FIELD_H;
  uint16_t bg = focus ? C_SEL : C_BG;
  tft.fillRect(0, y, 320, LT_FIELD_H, bg);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(L_BASELINE);
  tft.setTextColor(C_DIM, bg);
  tft.drawString(label, 12, y + 20);
  if (focus) {
    tft.fillTriangle(LT_VAL_X - 12, y + 15, LT_VAL_X - 6, y + 10, LT_VAL_X - 6, y + 20, C_TEXT);
    tft.fillTriangle(314, y + 15, 308, y + 10, 308, y + 20, C_TEXT);
  }
}

static void drawPanel(TFT_eSPI& tft, const LightsState& L) {
  const LightState& s = L.l[L.sel];
  bool ok = s.known && s.available;
  int right = L.editing ? 302 : 314;  // leave room for the right arrow
  for (int f = 0; f < 3; f++) {
    static const char* labels[] = {"Power", "Brightness", "Colour"};
    bool focus = L.editing && L.field == f;
    drawField(tft, f, labels[f], focus);
    int y = LT_PANEL_Y + f * LT_FIELD_H, base = y + 20;
    uint16_t bg = focus ? C_SEL : C_BG;
    tft.setTextDatum(R_BASELINE);
    tft.setTextColor(ok ? C_TEXT : C_DIM, bg);
    if (f == 0) {
      tft.drawString(!s.known ? "--" : !s.available ? "Unavailable" : s.on ? "On" : "Off", right, base);
    } else if (f == 1) {
      uint8_t pct = ok && s.on ? lightPct(s.brightness) : 0;
      char v[8];
      snprintf(v, sizeof(v), "%u%%", (unsigned)pct);
      int barW = right - 44 - LT_VAL_X;
      tft.drawRect(LT_VAL_X, y + 10, barW, 10, C_RULE);
      if (pct) tft.fillRect(LT_VAL_X + 1, y + 11, (barW - 2) * pct / 100, 8, C_LINE);
      tft.drawString(v, right, base);
    } else {
      int p = lightPresetIndex(s);
      uint16_t sw = lightSwatch565(s);
      if (sw) tft.fillRoundRect(LT_VAL_X, y + 7, 16, 16, 3, sw);
      else tft.drawRoundRect(LT_VAL_X, y + 7, 16, 16, 3, C_RULE);
      const char* name = !ok ? "--" : s.mode == LM_NONE ? "--" : p < 0 ? "Custom" : COLOUR_PRESETS[p].name;
      tft.drawString(name, right, base);
    }
  }
}

static void drawHint(TFT_eSPI& tft, const LightsState& L) {
  tft.fillRect(0, LT_HINT_Y, 320, 240 - LT_HINT_Y, C_BG);
  tft.setTextFont(1);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(L.editing ? "up/down: setting  left/right: change  press: back"
                           : "up/down: lamp  press: settings  left/right: page", 160, LT_HINT_Y + 2);
}

static void lightsDraw(TFT_eSPI& tft, LightsState& L, uint32_t now, HaStatus st, bool full) {
  if (full) tft.fillScreen(C_BG);
  drawHeader(tft, "Lights", now, st, full);
  bool ui = full || L.uiDirty;
  for (int i = 0; i < LIGHT_COUNT; i++)
    if (ui || (L.dirty & (1u << i))) drawLampRow(tft, L, i);
  if (full) tft.drawFastHLine(0, LT_PANEL_Y - 3, 320, C_RULE);
  if (ui || (L.dirty & (1u << L.sel))) drawPanel(tft, L);
  if (ui) drawHint(tft, L);
  L.dirty = 0;
  L.uiDirty = false;
}
