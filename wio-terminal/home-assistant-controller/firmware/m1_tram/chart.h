// Chart geometry for the 24 h step chart. Pure, host-tested (test/run.sh).
#pragma once
#include <math.h>
#include "tram_model.h"

struct ChartScale { int yMin, yMax; };

// [min-2, max+2] in whole minutes, never below 0; [0, 30] with no data.
inline ChartScale chartScale(const GapPoint* p, int n) {
  if (n <= 0) return {0, 30};
  float lo = p[0].v, hi = p[0].v;
  for (int i = 1; i < n; i++) { if (p[i].v < lo) lo = p[i].v; if (p[i].v > hi) hi = p[i].v; }
  int a = (int)floorf(lo) - 2, b = (int)ceilf(hi) + 2;
  if (a < 0) a = 0;
  return {a, b};
}

// x pixel for time t on a now-24h .. now axis starting at x0, w pixels wide.
inline int chartX(uint32_t t, uint32_t now, int x0, int w) {
  if (t >= now) return x0 + w;
  uint32_t age = now - t;
  if (age >= 86400) return x0;
  return x0 + w - (int)((uint64_t)age * w / 86400);
}

// y pixel for value v; y0 is the top of the plot, h its height.
inline int chartY(float v, ChartScale c, int y0, int h) {
  float f = (v - c.yMin) / (float)(c.yMax - c.yMin);
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  return y0 + h - (int)lroundf(f * h);
}
