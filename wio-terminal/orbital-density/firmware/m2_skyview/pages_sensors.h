// pages_sensors.h — display pages for the sky-observatory sensors. Included from
// m2_skyview.ino AFTER the sensor headers, spr/gps, the observation-log block and the
// existing draw helpers, so it uses them directly (single translation unit).
// Same 8-bit sprite, same built-in font (size 1 = 6x8 px, size 2 = 12x16 px).
#pragma once

// 24 h line chart of a Ring<int16_t,N>.v holding value*10. Auto-scaled with a minimum span
// (in value*10 units); labels left of the frame; UTC hour markers below, like the old charts.
void drawSeries(const int16_t* v, int len, int X, int Y, int W, int H, int minSpan, uint16_t colour, const char* unit) {
  spr.drawRect(X, Y, W, H, TFT_DARKGREY);
  spr.setTextSize(1);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (gps.time.isValid()) {
    int hh = gps.time.hour();
    int hrs[4] = { hh, (hh + 8) % 24, (hh + 16) % 24, hh };
    int xs[4]  = { X, X + W / 3, X + 2 * W / 3, X + W - 12 };
    for (int i = 0; i < 4; i++) { char t[4]; snprintf(t, sizeof(t), "%02d", hrs[i]); spr.drawString(t, xs[i], Y + H + 3); }
  } else {
    spr.drawString("-24h", X, Y + H + 3);
    spr.drawString("now", X + W - 18, Y + H + 3);
  }
  if (len < 2) { spr.drawString("collecting (1 sample / 5 min)", X + 40, Y + H / 2 - 4); return; }
  int lo = v[0], hi = v[0];
  for (int i = 1; i < len; i++) { if (v[i] < lo) lo = v[i]; if (v[i] > hi) hi = v[i]; }
  if (hi - lo < minSpan) { int mid = (hi + lo) / 2; lo = mid - minSpan / 2; hi = mid + minSpan / 2; }
  char lbl[16];
  spr.setTextColor(colour, TFT_BLACK);
  snprintf(lbl, sizeof(lbl), "%.1f%s", hi / 10.0f, unit); spr.drawString(lbl, 2, Y - 3);
  snprintf(lbl, sizeof(lbl), "%.1f%s", lo / 10.0f, unit); spr.drawString(lbl, 2, Y + H - 6);
  int px = -1, py = 0;
  for (int i = 0; i < len; i++) {
    int x = X + (int)((long)(W - 2) * i / (len - 1));
    int y = Y + H - 1 - (int)((long)(H - 2) * (v[i] - lo) / (hi - lo));
    if (px >= 0) spr.drawLine(px, py, x, y, colour);
    px = x; py = y;
  }
}

// Two-series 24 h chart of log10(value)x100 rings on ONE shared axis (never dual axes).
// Auto-scaled with a minimum span of one decade, dotted decade gridlines, axis labels as real
// magnitudes (%.2g), legend swatches top-left. Same hour markers as drawSeries().
void drawSeriesLog2(const int16_t* a, const int16_t* b, int len, int X, int Y, int W, int H,
                    uint16_t colA, const char* nameA, uint16_t colB, const char* nameB) {
  spr.drawRect(X, Y, W, H, TFT_DARKGREY);
  spr.setTextSize(1);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (gps.time.isValid()) {
    int hh = gps.time.hour();
    int hrs[4] = { hh, (hh + 8) % 24, (hh + 16) % 24, hh };
    int xs[4]  = { X, X + W / 3, X + 2 * W / 3, X + W - 12 };
    for (int i = 0; i < 4; i++) { char tt[4]; snprintf(tt, sizeof(tt), "%02d", hrs[i]); spr.drawString(tt, xs[i], Y + H + 3); }
  } else {
    spr.drawString("-24h", X, Y + H + 3);
    spr.drawString("now", X + W - 18, Y + H + 3);
  }
  spr.fillRect(X + 4, Y + 3, 6, 6, colA);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK); spr.drawString(nameA, X + 13, Y + 2);
  spr.fillRect(X + 46, Y + 3, 6, 6, colB);   spr.drawString(nameB, X + 55, Y + 2);
  if (len < 2) { spr.setTextColor(TFT_DARKGREY, TFT_BLACK); spr.drawString("collecting (1 sample / 5 min)", X + 80, Y + H / 2 - 4); return; }
  int lo = a[0], hi = a[0];
  for (int i = 0; i < len; i++) {
    if (a[i] < lo) lo = a[i]; if (a[i] > hi) hi = a[i];
    if (b[i] < lo) lo = b[i]; if (b[i] > hi) hi = b[i];
  }
  if (hi - lo < 100) { int mid = (hi + lo) / 2; lo = mid - 50; hi = mid + 50; }
  char lbl[16];
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  snprintf(lbl, sizeof(lbl), "%.2g", powf(10.0f, hi / 100.0f)); spr.drawString(lbl, 2, Y - 3);
  snprintf(lbl, sizeof(lbl), "%.2g", powf(10.0f, lo / 100.0f)); spr.drawString(lbl, 2, Y + H - 6);
  for (int d = (lo / 100) * 100; d <= hi; d += 100) {           // decade gridlines
    if (d <= lo || d >= hi) continue;
    int gy = Y + H - 1 - (int)((long)(H - 2) * (d - lo) / (hi - lo));
    for (int gx = X + 2; gx < X + W - 2; gx += 8) spr.drawPixel(gx, gy, TFT_DARKGREY);
  }
  for (int k = 0; k < 2; k++) {
    const int16_t* v = k ? b : a; uint16_t col = k ? colB : colA;
    int px = -1, py = 0;
    for (int i = 0; i < len; i++) {
      int x = X + (int)((long)(W - 2) * i / (len - 1));
      int y = Y + H - 1 - (int)((long)(H - 2) * (v[i] - lo) / (hi - lo));
      if (px >= 0) spr.drawLine(px, py, x, y, col);
      px = x; py = y;
    }
  }
}

// PROVISIONAL sky-condition label from the thermal delta only. These thresholds are
// placeholders until field calibration produces a local baseline (README). Not a cloud index.
const float SKY_CLEAR_DELTA_C  = 20.0f;   // ambient - skyIR above this: probably clear
const float SKY_CLOUDY_DELTA_C = 8.0f;    // below this: probably overcast or obstructed

const char* skyConditionLabel() {
#if MLX_ENABLED
  if (!mlxOk || isnan(mlxDeltaC)) return "--";
  if (mlxDeltaC >= SKY_CLEAR_DELTA_C)  return "CLEAR?";
  if (mlxDeltaC <= SKY_CLOUDY_DELTA_C) return "CLOUDY?";
  return "MIXED?";
#else
  return "--";                 // needs the thermal channel (GY-906 deferred)
#endif
}

void drawSkySensors() {
  spr.fillRect(0, 20, 320, 220, TFT_BLACK);
  char l[48]; int y = 26;
  spr.setTextSize(2);
  if (!tslOk) {
    spr.setTextColor(TFT_RED, TFT_BLACK); spr.drawString("TSL2591 missing", 8, y); y += 24;
  } else {
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    if (isnan(tslLux))      snprintf(l, sizeof(l), "Visible  %s", tslSat ? "SATURATED" : "--");
    else if (tslLux < 1.0f) snprintf(l, sizeof(l), "Visible  %.4f lx", tslLux);
    else                    snprintf(l, sizeof(l), "Visible  %.1f lx", tslLux);
    spr.drawString(l, 8, y); y += 24;
    float irFrac = tslFull ? 100.0f * tslIr / tslFull : 0.0f;
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    snprintf(l, sizeof(l), "Near IR  %.1f %%", irFrac); spr.drawString(l, 8, y); y += 24;
    spr.setTextSize(1); spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    snprintf(l, sizeof(l), "raw full %u  ir %u  gain %s  %u ms", tslFull, tslIr, tslGainName(), tslIntegMs);
    spr.drawString(l, 8, y); y += 14;
    spr.setTextSize(2);
  }
#if MLX_ENABLED
  if (!mlxOk) {
    spr.setTextColor(TFT_RED, TFT_BLACK); spr.drawString("MLX90614 missing", 8, y); y += 24;
  } else {
    spr.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(l, sizeof(l), "Sky IR   %.1f C", mlxObjC); spr.drawString(l, 8, y); y += 24;
    spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    snprintf(l, sizeof(l), "Delta %.1f C amb %.1f", mlxDeltaC, mlxAmbC); spr.drawString(l, 8, y); y += 24;
  }
#else
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK); spr.drawString("Sky IR   not installed", 8, y); y += 24;
#endif
  spr.setTextColor(TFT_GREEN, TFT_BLACK);
  snprintf(l, sizeof(l), "Condition %s", skyConditionLabel()); spr.drawString(l, 8, y); y += 20;
  // Charts: the light chart (Task 16) always; the thermal chart shares the space when the
  // MLX90614 is enabled and there is room for both, otherwise light wins (revisit the layout
  // when the thermal channel arrives — a page of its own is the likely answer).
  spr.setTextSize(1); spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  int avail = 224 - 12 - y;                    // rows left above the anomaly-banner strip
  int hLight = avail - 12, hTherm = 0;
#if MLX_ENABLED
  if ((avail - 36) / 2 >= 24) { hLight = (avail - 36) / 2; hTherm = avail - 36 - hLight; }
#endif
  spr.drawString("sky light, 24 h  (log, gain-normalised counts/ms)", 8, y); y += 12;
  if (hLight >= 24) drawSeriesLog2(tslVisHist.v, tslIrHist.v, tslVisHist.len, 42, y, 268, hLight, TFT_CYAN, "vis", TFT_ORANGE, "IR");
  y += hLight + 12;
#if MLX_ENABLED
  if (hTherm >= 24) {
    spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    spr.drawString("thermal delta, 24 h  (thresholds provisional)", 8, y); y += 12;
    drawSeries(mlxDeltaHist.v, mlxDeltaHist.len, 42, y, 268, hTherm, 50, TFT_YELLOW, "C");
  }
#endif
}

void drawMag() {
  spr.fillRect(0, 20, 320, 220, TFT_BLACK);
  char l[64]; int y = 26;
  spr.setTextSize(2);
  if (!magOk) { spr.setTextColor(TFT_RED, TFT_BLACK); spr.drawString("MMC5603 missing", 8, y); return; }
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(l, sizeof(l), "|B| %.2f uT", magMeanTotal); spr.drawString(l, 8, y);
  spr.setTextColor(TFT_GREEN, TFT_BLACK);
  snprintf(l, sizeof(l), "Hdg %.0f", magMeanHeading); spr.drawString(l, 200, y); y += 24;
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  snprintf(l, sizeof(l), "X %7.2f  Y %7.2f", magMeanX, magMeanY); spr.drawString(l, 8, y); y += 24;
  snprintf(l, sizeof(l), "Z %7.2f", magMeanZ); spr.drawString(l, 8, y); y += 24;
  spr.setTextSize(1); spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  snprintf(l, sizeof(l), "%lu samples  %lu err  off %.1f/%.1f/%.1f  level assumed",
           (unsigned long)magSampleCount, (unsigned long)magErrCount, MAG_OFF_X, MAG_OFF_Y, MAG_OFF_Z);
  spr.drawString(l, 8, y); y += 14;
  spr.drawString("|B| uT, 24 h", 8, y); y += 12;
  drawSeries(magTotalHist.v, magTotalHist.len, 42, y, 268, 224 - 12 - y, 20, TFT_MAGENTA, "");
}

void drawSensors() {
  spr.fillRect(0, 20, 320, 220, TFT_BLACK);
  char l[64]; int y = 26;
  spr.setTextSize(2); spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("SENSORS  I2C 100k", 8, y); y += 26;
  struct Row { const char* name; uint8_t addr; bool ok; uint32_t lastMs; };
  Row rows[] = {
    {"TSL2591 optical", 0x29,     tslOk, tslLastMs},
    {"MMC5603 mag",     0x30,     magOk, magLastMs},
#if MLX_ENABLED
    {"MLX90614 IR",     0x5A,     mlxOk, mlxLastMs},
#endif
    {"BME280 env",      BME_ADDR, bmeOk, millis()},
    {"BQ27441 batt",    0x55,     batOk, millis()},
  };
  spr.setTextSize(1);
  for (auto& r : rows) {
    spr.setTextColor(r.ok ? TFT_GREEN : TFT_RED, TFT_BLACK);
    unsigned long age = r.ok ? (millis() - r.lastMs) / 1000 : 0;
    snprintf(l, sizeof(l), "0x%02X %-16s %s  %lus ago", r.addr, r.name, r.ok ? "OK " : "---", age);
    spr.drawString(l, 8, y); y += 14;
  }
  y += 6;
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString("boot scan:", 8, y);
  int x = 72;
  for (uint8_t i = 0; i < i2cFoundCount && x < 300; i++) { snprintf(l, sizeof(l), "0x%02X", i2cFound[i]); spr.drawString(l, x, y); x += 32; }
  y += 18;
  snprintf(l, sizeof(l), "loop max %lu ms   iter/s %lu", (unsigned long)loopMaxMsLast, (unsigned long)loopIterLast); spr.drawString(l, 8, y); y += 14;
  snprintf(l, sizeof(l), "NMEA pass %lu  fail %lu", (unsigned long)gps.passedChecksum(), (unsigned long)gps.failedChecksum()); spr.drawString(l, 8, y); y += 14;
  snprintf(l, sizeof(l), "obs rows %lu  write err %lu  buffered %u", (unsigned long)obsRowsWritten, (unsigned long)obsWriteErrors, (unsigned)obsRowsBuffered); spr.drawString(l, 8, y); y += 14;
#if MLX_ENABLED
  snprintf(l, sizeof(l), "tsl err %lu  mag err %lu  mlx err %lu", (unsigned long)tslErrCount, (unsigned long)magErrCount, (unsigned long)mlxErrCount);
#else
  snprintf(l, sizeof(l), "tsl err %lu  mag err %lu  (MLX90614 not installed)", (unsigned long)tslErrCount, (unsigned long)magErrCount);
#endif
  spr.drawString(l, 8, y);
}
