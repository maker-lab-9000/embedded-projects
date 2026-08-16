// Milestone 4 — SD persistence on top of M3 trends.
//
// Every committed 1-min sample is appended to /soil.csv on the microSD card
// (format: boot,minute,sensor,raw,pct). On boot the last 48 h of samples are read
// back into the ring buffer, so the sparkline and long-term record survive
// reboots. The drying-rate segment intentionally restarts at each boot: with
// no battery-backed clock the off-time is unknown, and a slope fitted across
// an unknown gap would be wrong. CSV `minute` keeps counting up across
// boots, but real elapsed time across a boot boundary is unknown.
//
// No card (or card error): runs exactly like M3, shows "SD!" top-right, and
// retries the card once a minute. FAT32 card recommended.

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

const char* LOG_PATH = "/soil.csv";
const int   SENSOR_ID = 1;
const uint32_t RESTORE_TAIL_BYTES = 90000;  // ~ >2880 lines of CSV

TFT_eSPI tft;

float history[HISTORY_LEN];
long  totalSamples = 0;
long  segmentStart = 0;

bool  sdOk = false;
int   bootId = 1;
long  minuteIdx = 0;  // persisted counter, continues across boots

float    accumPct = 0;
int      accumN = 0;
uint32_t lastLogMs = 0;
uint32_t lastStatusMs = 0;
bool     lowAlerted = false;

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

// Reload the tail of /soil.csv into the ring buffer.
void restoreHistory() {
  if (!sdOk || !SD.exists(LOG_PATH)) {
    if (sdOk) {  // brand-new card: create file with header
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
    f.readStringUntil('\n');  // discard partial line
  }
  long lastBoot = 0, lastMinute = -1;
  while (f.available()) {
    String line = f.readStringUntil('\n');
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

void chirp() {
  tone(WIO_BUZZER, 1800, 120);
  delay(180);
  tone(WIO_BUZZER, 1400, 200);
}

// ---------- main ----------

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  pinMode(SENSOR_PIN, INPUT);
  pinMode(WIO_BUZZER, OUTPUT);
  pinMode(SDCARD_DET_PIN, INPUT_PULLUP);  // slot's mechanical card-detect switch

  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Soil Moisture  #1", 60, 8);

  initSd();
  restoreHistory();
  drawSdBadge();
  drawSparkline();
  if (totalSamples > 0) drawTrend(histAt(totalSamples - 1));

  lastLogMs = millis();
}

void loop() {
  uint16_t raw = readSensorRaw();
  float pct = rawToPercent(raw);
  accumPct += pct;
  accumN++;

  drawLive(pct, raw);

  if (millis() - lastLogMs >= LOG_PERIOD_MS && accumN > 0) {
    lastLogMs += LOG_PERIOD_MS;
    float mean = accumPct / accumN;
    accumPct = 0;
    accumN = 0;

    commitToHistory(mean);

    if (!sdOk) initSd();               // retry a missing/late-inserted card
    if (sdOk && !appendLog(raw, mean)) {
      sdOk = false;                    // write failed: card pulled?
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

  if (millis() - lastStatusMs >= 5000) {
    lastStatusMs = millis();
    Serial.print("raw=");
    Serial.print(raw);
    Serial.print("  pct=");
    Serial.print(pct, 1);
    Serial.print("  sd=");
    Serial.print(sdOk ? "ok" : "none");
    Serial.print("  det=");
    Serial.println(digitalRead(SDCARD_DET_PIN));
  }

  delay(1000);
}
