// Milestone 5 — portable power: screen toggle + on-device trend reset.
//
// Adds to m4_sdlog:
//  - 5-way switch center press: LCD backlight on/off. The backlight is the
//    dominant power draw; its pin (PC05) has no hardware PWM, so it's a
//    toggle rather than a dimmer. Logging continues with the screen off.
//  - Top-left button (KEY_C) held 3 s: restart trends after moving the
//    probe. Clears RAM history, appends a "#RESET,<minute>" marker to
//    /soil.csv (restore-on-boot ignores everything before the last marker),
//    and chirps to confirm. Calibration anchors are untouched.
//
// CSV format: boot,minute,sensor,raw,pct  (+ "#RESET,<minute>" marker rows).
// `minute` does not advance while powered off (no battery-backed clock).

#include <TFT_eSPI.h>
#include <Seeed_FS.h>
#include "SD/Seeed_SD.h"

// ---- calibration (sensor #1, see CALIBRATION.md) ----
const uint16_t RAW_AIR   = 3687;  // 0 %
const uint16_t RAW_WATER = 1568;  // 100 %
const int SENSOR_PIN = A0;

// ---- behaviour ----
const float    WATER_THRESHOLD  = 30.0f;
const uint32_t LOG_PERIOD_MS    = 60000;
const int      HISTORY_LEN      = 2880;    // 48 h of 1-min samples
const long     RATE_WINDOW      = 360;     // fit over at most 6 h
const long     RATE_MIN_SAMPLES = 30;
const float    WATERING_JUMP    = 5.0f;
const uint32_t LONG_PRESS_MS    = 3000;

const char* LOG_PATH = "/soil.csv";
const int   SENSOR_ID = 1;
const uint32_t RESTORE_TAIL_BYTES = 90000;

TFT_eSPI tft;

float history[HISTORY_LEN];
long  totalSamples = 0;
long  segmentStart = 0;

bool  sdOk = false;
int   bootId = 1;
long  minuteIdx = 0;

float    accumPct = 0;
int      accumN = 0;
float    lastMean = 0;
uint32_t lastLogMs = 0;
uint32_t lastSenseMs = 0;
uint32_t lastStatusMs = 0;
bool     lowAlerted = false;

bool     backlightOn = true;
uint32_t keyCPressedAt = 0;
bool     keyCFired = false;
bool     prev5s = HIGH;

float histAt(long absIdx) { return history[absIdx % HISTORY_LEN]; }

// ---------- sensor ----------

uint16_t readSensorRaw() {
  const int N = 15;
  uint16_t s[N];
  for (int i = 0; i < N; i++) {
    s[i] = analogRead(SENSOR_PIN);
    delay(2);
  }
  for (int i = 1; i < N; i++) {
    uint16_t v = s[i];
    int j = i - 1;
    while (j >= 0 && s[j] > v) {
      s[j + 1] = s[j];
      j--;
    }
    s[j + 1] = v;
  }
  return s[N / 2];
}

float rawToPercent(uint16_t raw) {
  float pct = 100.0f * (float)(RAW_AIR - raw) / (float)(RAW_AIR - RAW_WATER);
  return constrain(pct, 0.0f, 100.0f);
}

// ---------- trend ----------

void commitToHistory(float pct) {
  history[totalSamples % HISTORY_LEN] = pct;
  totalSamples++;
  if (totalSamples >= 6 && pct - histAt(totalSamples - 6) > WATERING_JUMP) {
    segmentStart = totalSamples - 1;
  }
  long floorIdx = totalSamples - min(RATE_WINDOW, (long)HISTORY_LEN);
  if (segmentStart < floorIdx) segmentStart = floorIdx;
  if (segmentStart < 0) segmentStart = 0;
}

bool slopePerHour(float &out) {
  long n = totalSamples - segmentStart;
  if (n < RATE_MIN_SAMPLES) return false;
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (long i = segmentStart; i < totalSamples; i++) {
    double x = (double)(i - segmentStart);
    double y = histAt(i);
    sx += x; sy += y; sxx += x * x; sxy += x * y;
  }
  double denom = (double)n * sxx - sx * sx;
  if (denom == 0) return false;
  out = (float)(((double)n * sxy - sx * sy) / denom * 60.0);
  return true;
}

// ---------- SD ----------

void drawSdBadge() {
  if (!backlightOn) return;
  tft.setTextSize(2);
  if (sdOk) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("SD ", 284, 8);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("SD!", 284, 8);
  }
}

bool initSd() {
  sdOk = SD.begin(SDCARD_SS_PIN, SDCARD_SPI);
  return sdOk;
}

void restoreHistory() {
  if (!sdOk || !SD.exists(LOG_PATH)) {
    if (sdOk) {
      File f = SD.open(LOG_PATH, FILE_APPEND);
      if (f) {
        f.println("boot,minute,sensor,raw,pct");
        f.close();
      }
    }
    return;
  }
  File f = SD.open(LOG_PATH, FILE_READ);
  if (!f) return;
  uint32_t size = f.size();
  if (size > RESTORE_TAIL_BYTES) {
    f.seek(size - RESTORE_TAIL_BYTES);
    f.readStringUntil('\n');
  }
  long lastBoot = 0, lastMinute = -1;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.startsWith("#RESET")) {
      totalSamples = 0;  // probe was moved: discard everything before this
      continue;
    }
    int b, sensor;
    long m;
    unsigned int raw;
    float pct;
    if (sscanf(line.c_str(), "%d,%ld,%d,%u,%f", &b, &m, &sensor, &raw, &pct) == 5) {
      history[totalSamples % HISTORY_LEN] = pct;
      totalSamples++;
      lastBoot = b;
      lastMinute = m;
    }
  }
  f.close();
  bootId = (int)lastBoot + 1;
  minuteIdx = lastMinute + 1;
  segmentStart = totalSamples;  // trend restarts: off-time is unknown
  if (totalSamples > 0) lowAlerted = histAt(totalSamples - 1) <= WATER_THRESHOLD;
  Serial.print("RESTORED,");
  Serial.print(totalSamples);
  Serial.print(",boot=");
  Serial.println(bootId);
}

bool appendLog(uint16_t raw, float pct) {
  File f = SD.open(LOG_PATH, FILE_APPEND);
  if (!f) return false;
  f.print(bootId);
  f.print(',');
  f.print(minuteIdx);
  f.print(',');
  f.print(SENSOR_ID);
  f.print(',');
  f.print(raw);
  f.print(',');
  f.println(pct, 2);
  f.close();
  return true;
}

// ---------- display ----------

const int SPARK_X = 20, SPARK_Y = 140, SPARK_W = 280, SPARK_H = 55;

int sparkY(float pct) {
  return SPARK_Y + SPARK_H - 2 - (int)((SPARK_H - 4) * pct / 100.0f);
}

void drawSparkline() {
  if (!backlightOn) return;
  tft.fillRect(SPARK_X, SPARK_Y, SPARK_W, SPARK_H, TFT_BLACK);
  tft.drawRect(SPARK_X, SPARK_Y, SPARK_W, SPARK_H, TFT_DARKGREY);
  int yThr = sparkY(WATER_THRESHOLD);
  for (int x = SPARK_X + 2; x < SPARK_X + SPARK_W - 2; x += 6) {
    tft.drawPixel(x, yThr, TFT_RED);
  }
  long span = min(totalSamples, (long)HISTORY_LEN);
  if (span < 2) return;
  long stride = max(1L, span / (long)(SPARK_W - 4));
  long cols = span / stride;
  int prevX = -1, prevY = 0;
  for (long c = 0; c < cols; c++) {
    long idx = totalSamples - span + c * stride;
    int x = SPARK_X + 2 + (int)((long)(SPARK_W - 4) * c / max(cols - 1, 1L));
    int y = sparkY(histAt(idx));
    if (prevX >= 0) tft.drawLine(prevX, prevY, x, y, TFT_CYAN);
    prevX = x; prevY = y;
  }
}

void drawTrend(float pct) {
  if (!backlightOn) return;
  char line[40];
  tft.setTextSize(2);

  float rate;
  bool haveRate = slopePerHour(rate);
  if (!haveRate) {
    long have = totalSamples - segmentStart;
    snprintf(line, sizeof(line), "Rate: collecting %2ld/%ldm ", have, RATE_MIN_SAMPLES);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString(line, 20, 92);
    tft.drawString("ETA:  --                ", 20, 116);
    return;
  }

  snprintf(line, sizeof(line), "Rate: %+5.2f %%/h        ", rate);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(line, 20, 92);

  if (pct <= WATER_THRESHOLD) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("WATER NOW!              ", 20, 116);
  } else if (rate >= -0.005f) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("ETA:  not drying         ", 20, 116);
  } else {
    float hours = (pct - WATER_THRESHOLD) / -rate;
    if (hours > 99 * 24) {
      snprintf(line, sizeof(line), "ETA:  >99 days           ");
    } else {
      int d = (int)(hours / 24), h = (int)hours % 24;
      snprintf(line, sizeof(line), "Water in ~%dd %02dh       ", d, h);
    }
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(line, 20, 116);
  }
}

void drawLive(float pct, uint16_t raw) {
  if (!backlightOn) return;
  uint16_t color = pct < WATER_THRESHOLD ? TFT_RED
                 : (pct < 50 ? TFT_YELLOW : TFT_GREEN);
  char buf[8];
  snprintf(buf, sizeof(buf), "%3d%%", (int)(pct + 0.5f));
  tft.setTextSize(6);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(buf, 88, 34);

  char diag[40];
  uint32_t upMin = millis() / 60000;
  snprintf(diag, sizeof(diag), "raw %4u   up %lu:%02lu   ", raw,
           (unsigned long)(upMin / 60), (unsigned long)(upMin % 60));
  tft.setTextSize(2);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(diag, 20, 214);
}

void drawHeader() {
  if (!backlightOn) return;
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Soil Moisture  #1", 60, 8);
}

void chirp() {
  tone(WIO_BUZZER, 1800, 120);
  delay(180);
  tone(WIO_BUZZER, 1400, 200);
}

// ---------- controls ----------

void setBacklight(bool on) {
  backlightOn = on;
  digitalWrite(LCD_BACKLIGHT, on ? HIGH : LOW);
  if (on) {  // full redraw with current state
    tft.fillScreen(TFT_BLACK);
    drawHeader();
    drawSdBadge();
    drawSparkline();
    drawTrend(lastMean);
  }
}

// After moving the probe: forget the old spot's history, mark it on SD.
void restartTrends() {
  totalSamples = 0;
  segmentStart = 0;
  accumPct = 0;
  accumN = 0;
  if (sdOk) {
    File f = SD.open(LOG_PATH, FILE_APPEND);
    if (f) {
      f.print("#RESET,");
      f.println(minuteIdx);
      f.close();
    }
  }
  Serial.print("TRENDS_RESET,");
  Serial.println(minuteIdx);
  chirp();
  drawSparkline();
  drawTrend(lastMean);
}

void pollButtons() {
  bool cur5s = digitalRead(WIO_5S_PRESS);
  if (prev5s == HIGH && cur5s == LOW) setBacklight(!backlightOn);
  prev5s = cur5s;

  if (digitalRead(WIO_KEY_C) == LOW) {
    if (keyCPressedAt == 0) {
      keyCPressedAt = millis();
      keyCFired = false;
    } else if (!keyCFired && millis() - keyCPressedAt >= LONG_PRESS_MS) {
      keyCFired = true;
      restartTrends();
    }
  } else {
    keyCPressedAt = 0;
  }
}

// ---------- main ----------

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  pinMode(SENSOR_PIN, INPUT);
  pinMode(WIO_BUZZER, OUTPUT);
  pinMode(SDCARD_DET_PIN, INPUT_PULLUP);
  pinMode(WIO_5S_PRESS, INPUT_PULLUP);
  pinMode(WIO_KEY_C, INPUT_PULLUP);
  pinMode(LCD_BACKLIGHT, OUTPUT);
  digitalWrite(LCD_BACKLIGHT, HIGH);

  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  drawHeader();

  initSd();
  restoreHistory();
  drawSdBadge();
  drawSparkline();
  if (totalSamples > 0) {
    lastMean = histAt(totalSamples - 1);
    drawTrend(lastMean);
  }

  lastLogMs = millis();
}

void loop() {
  pollButtons();  // every ~25 ms so presses feel instant

  if (millis() - lastSenseMs >= 1000) {
    lastSenseMs = millis();
    uint16_t raw = readSensorRaw();
    float pct = rawToPercent(raw);
    accumPct += pct;
    accumN++;
    drawLive(pct, raw);

    if (millis() - lastStatusMs >= 5000) {
      lastStatusMs = millis();
      Serial.print("raw=");
      Serial.print(raw);
      Serial.print("  pct=");
      Serial.print(pct, 1);
      Serial.print("  sd=");
      Serial.print(sdOk ? "ok" : "none");
      Serial.print("  bl=");
      Serial.println(backlightOn ? "on" : "off");
    }

    if (millis() - lastLogMs >= LOG_PERIOD_MS && accumN > 0) {
      lastLogMs += LOG_PERIOD_MS;
      float mean = accumPct / accumN;
      accumPct = 0;
      accumN = 0;
      lastMean = mean;

      commitToHistory(mean);

      if (!sdOk) initSd();
      if (sdOk && !appendLog(raw, mean)) {
        sdOk = false;
      }
      drawSdBadge();
      minuteIdx++;

      drawSparkline();
      drawTrend(mean);

      float rate = 0;
      bool haveRate = slopePerHour(rate);
      Serial.print("LOG,");
      Serial.print(minuteIdx - 1);
      Serial.print(",");
      Serial.print(raw);
      Serial.print(",");
      Serial.print(mean, 2);
      Serial.print(",");
      if (haveRate) Serial.println(rate, 3); else Serial.println("na");

      if (mean <= WATER_THRESHOLD && !lowAlerted) {
        lowAlerted = true;
        chirp();
      } else if (mean > WATER_THRESHOLD + 5) {
        lowAlerted = false;
      }
    }
  }

  delay(25);
}
