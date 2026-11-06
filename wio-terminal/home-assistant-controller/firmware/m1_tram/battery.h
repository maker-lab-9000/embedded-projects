// Battery chassis fuel gauge (BQ27441, 650 mAh pack) on Wire. Optional: without
// the chassis batteryOk() stays false and no meter is drawn.
#pragma once
#include <Wire.h>
#include <SparkFunBQ27441.h>
#include "battery_meter.h"

#define BATTERY_POLL_MS 30000UL

static bool batOk = false;
static int batSoc = -1;          // percent, -1 = not read yet
static bool batCharging = false;
static uint32_t batNextMs = 0;

inline bool batteryOk() { return batOk; }

inline void batteryInit() {
  Wire.begin();
  batOk = lipo.begin();
  if (batOk) lipo.setCapacity(650);
  Serial.print("battery gauge: "); Serial.println(batOk ? "found" : "none");
}

// Returns true when the reading changed (header needs a redraw).
inline bool batteryPoll() {
  if (!batOk || (int32_t)(millis() - batNextMs) < 0) return false;
  batNextMs = millis() + BATTERY_POLL_MS;
  int soc = lipo.soc();
  bool chg = lipo.current(AVG) > 0;  // positive average current = charging
  bool changed = soc != batSoc || chg != batCharging;
  batSoc = soc;
  batCharging = chg;
  return changed;
}

// Battery icon, 22x12 body plus nub, at (x, y) top-left.
template <class Gfx>
inline void batteryDraw(Gfx& tft, int x, int y, uint16_t bg, uint16_t outline) {
  tft.fillRect(x, y, 26, 12, bg);
  if (!batOk || batSoc < 0) return;
  tft.drawRect(x, y, 22, 12, outline);
  tft.fillRect(x + 22, y + 3, 2, 6, outline);
  int w = batteryFillPx(batSoc, 18);
  if (w) tft.fillRect(x + 2, y + 2, w, 8, batteryColor(batSoc));
  if (batCharging) {  // small lightning bolt
    tft.fillTriangle(x + 12, y + 1, x + 7, y + 7, x + 11, y + 7, outline);
    tft.fillTriangle(x + 10, y + 11, x + 15, y + 5, x + 11, y + 5, outline);
  }
}
