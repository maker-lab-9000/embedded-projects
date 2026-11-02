// Shared page header: battery (if present), page title, HA status dot and
// label, Berlin clock. Redraws only when one of them changed.
#pragma once
#include <TFT_eSPI.h>
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
#define C_SEL    0x2945  // #2a2a2a, selected row

#define Y_HEAD   0
#define H_HEAD   24

struct HeaderView {
  char clock[8] = "";
  HaStatus status = (HaStatus)255;
};
static HeaderView hdr;

inline void drawHeader(TFT_eSPI& tft, const char* title, uint32_t now, HaStatus st, bool full) {
  char clk[8];
  formatClock(now, clk, sizeof(clk));
  bool bat = batteryPoll();
  if (!full && !bat && st == hdr.status && !strcmp(clk, hdr.clock)) return;
  tft.fillRect(0, Y_HEAD, 320, H_HEAD, C_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(C_TEXT);
  tft.setTextDatum(L_BASELINE);
  batteryDraw(tft, 6, 6, C_BG, C_TEXT);  // top-left corner
  tft.drawString(title, batteryOk() ? 38 : 6, 17);
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
  strcpy(hdr.clock, clk);
  hdr.status = st;
}

