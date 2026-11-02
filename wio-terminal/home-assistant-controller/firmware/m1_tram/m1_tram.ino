// Wio Terminal Home Assistant controller: Tram, Climate, Lights, Heating and Power pages.
// See README.md. Secrets in secrets.h (copy secrets.h.example).
#include <TFT_eSPI.h>
#include "net.h"
#include "ha.h"
#include "page_tram.h"
#include "page_climate.h"
#include "page_lights.h"
#include "page_heating.h"
#include "page_power.h"
#include "ui.h"
#include "wdt.h"

static TFT_eSPI tft;
static TramState tram{};
static ClimateState climate{};
static LightsState lights{};
static HeatingState heating{};
static PowerState power{};

static void drawTramPage(bool full) {
  uint32_t t0 = millis();
  tramDraw(tft, tram, nowUtc(), haStatus(), full);
  uint32_t dt = millis() - t0;
  if (dt > 40 || full) { Serial.print("draw ms "); Serial.print(dt); Serial.println(full ? " (full)" : ""); }
}

static void drawClimatePage(bool full) {
  climateDraw(tft, climate, nowUtc(), haStatus(), full);
}

static void drawLightsPage(bool full) {
  lightsDraw(tft, lights, nowUtc(), haStatus(), full);
}

static bool lightsKey(Key k) {
  LightAction a;
  if (!lightsOnKey(lights, k, a)) return false;
  if (a.kind != LA_NONE) {
    if (haCallLight(a)) lightsApplyAction(lights, a);
    else Serial.println("lights: not connected, nothing sent");
  }
  return true;
}

static void drawHeatingPage(bool full) {
  heatingDraw(tft, heating, nowUtc(), haStatus(), full);
}

static void sendHeating(HeatAction a) {
  if (a.kind == HA_NONE_ACT) return;
  if (!haCallHeating(a)) { Serial.println("heating: not connected, nothing sent"); return; }
  if (a.kind == HA_SET_MODE) heatingApplyAction(heating, a);  // targets are applied by the model
}

static bool heatingKey(Key k) {
  HeatAction a;
  if (!heatingOnKey(heating, k, millis(), a)) return false;
  sendHeating(a);
  return true;
}

static void drawPowerPage(bool full) {
  powerDraw(tft, power, nowUtc(), haStatus(), full);
}

static const Page PAGES[] = {
  {"Tram", drawTramPage, nullptr},
  {"Climate", drawClimatePage, nullptr},
  {"Lights", drawLightsPage, lightsKey},
  {"Heating", drawHeatingPage, heatingKey},
  {"Power", drawPowerPage, nullptr},
};

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) {}
  Serial.println("\n== m1_tram ==");
  if (wdtCausedLastReset()) Serial.println("reset by watchdog");
  wdtReport();
  wdtInit();
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(C_BG);
  batteryInit();
  uiInit(PAGES, sizeof(PAGES) / sizeof(PAGES[0]));
  uiPoll();  // first paint before the (blocking) WiFi scan
  netInit();
  haInit(&tram, &climate, &lights, &heating, &power);
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
  wdtFeed();
  wdtCrumb(1);
  netPoll();
  wdtCrumb(4);
  haPoll();
  wdtCrumb(5);
  uiPoll();
  wdtCrumb(6);
  HeatAction due;
  if (heatingDue(heating, millis(), due)) sendHeating(due);  // target, after the presses stop
  if (Serial.available()) {
    int c = Serial.read();
    if (c == 'r') haForceReconnect();   // test reconnects
    if (c == 's') screenshot();
    if (c == 'p') { uiNextPage(); Serial.print("page "); Serial.println(uiPage); }  // no joystick needed
    if (c == 'c') {                     // uptime and watchdog breadcrumbs
      Serial.print("uptime s "); Serial.print(millis() / 1000);
      Serial.print(" ha "); Serial.print(haStatus());
      Serial.print(" early warnings "); Serial.println(WDT_CRUMBS[2]);
    }
    // Simulated keys: U/D/L/R/P joystick, B button B (L/R change lamps when editing).
    static const char KEYS[] = "BUDLRP";
    static const Key KEYV[] = {KEY_B, KEY_JOY_UP, KEY_JOY_DOWN, KEY_JOY_LEFT, KEY_JOY_RIGHT, KEY_JOY_PRESS};
    if (const char* k = c ? strchr(KEYS, c) : nullptr) uiInjectKey(KEYV[k - KEYS]);
    if (c == 'w') { Serial.println("hanging to test the watchdog"); while (true) {} }
  }
}
