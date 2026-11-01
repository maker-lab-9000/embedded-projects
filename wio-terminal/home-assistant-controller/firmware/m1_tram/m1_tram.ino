// Wio Terminal Home Assistant controller: Tram page (v1).
// See README.md. Secrets in secrets.h (copy secrets.h.example).
#include <TFT_eSPI.h>
#include "net.h"
#include "ha.h"
#include "page_tram.h"
#include "ui.h"

static TFT_eSPI tft;
static TramState tram{};

static void drawTramPage(bool full) {
  uint32_t t0 = millis();
  tramDraw(tft, tram, nowUtc(), haStatus(), full);
  uint32_t dt = millis() - t0;
  if (dt > 40 || full) { Serial.print("draw ms "); Serial.print(dt); Serial.println(full ? " (full)" : ""); }
}

static const Page PAGES[] = {
  {"Tram", drawTramPage, nullptr},
};

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) {}
  Serial.println("\n== m1_tram ==");
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(C_BG);
  uiInit(PAGES, sizeof(PAGES) / sizeof(PAGES[0]));
  uiPoll();  // first paint before the (blocking) WiFi scan
  netInit();
  haInit(&tram);
}

// Serial 's': raw RGB565 screenshot ("SHOT\n" + 320*240*2 bytes, big-endian rows).
static void screenshot() {
  static uint16_t row[320];
  Serial.print("SHOT\n");
  for (int y = 0; y < 240; y++) {
    tft.readRect(0, y, 320, 1, row);
    Serial.write(reinterpret_cast<uint8_t*>(row), sizeof(row));
  }
  Serial.flush();
}

void loop() {
  netPoll();
  haPoll();
  uiPoll();
  if (Serial.available()) {
    int c = Serial.read();
    if (c == 'r') haForceReconnect();   // test reconnects
    if (c == 's') screenshot();
  }
}
