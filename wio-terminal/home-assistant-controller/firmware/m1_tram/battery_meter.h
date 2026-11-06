// Battery meter math. Pure, host-tested (test/run.sh).
#pragma once
#include <stdint.h>

// Filled pixels for a state of charge in percent; any charge above 0 shows at least 1 px.
inline int batteryFillPx(int soc, int w) {
  if (soc <= 0) return 0;
  if (soc >= 100) return w;
  int px = (soc * w + 50) / 100;
  return px < 1 ? 1 : px;
}

// RGB565: green >= 30 %, yellow >= 15 %, red below.
inline uint16_t batteryColor(int soc) {
  return soc >= 30 ? 0x07E0 : soc >= 15 ? 0xFFE0 : 0xF800;
}
