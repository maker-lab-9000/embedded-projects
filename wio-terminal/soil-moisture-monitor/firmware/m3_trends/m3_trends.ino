// Milestone 3 — trends: history buffer, watering detection, drying rate, ETA.
//
// Sensor #1 on A0 (physical pin 13), calibration from CALIBRATION.md.
// History lives in RAM only (cleared on reboot); SD persistence is M4.
// millis() rollover (~49 days) is not handled — power-cycle before then.
//
// Behaviour:
//  - reads the sensor once per second (median of 15), shows live % on LCD
//  - commits the 1-minute mean to a 48 h ring buffer
//  - a rise of >5 points within 5 minutes = watering event -> trend restarts
//  - drying rate = least-squares slope over the trailing 6 h (min 30 min)
//  - ETA = time until the trend line crosses WATER_THRESHOLD
//  - two chirps when moisture falls below the threshold (hysteresis +5)
//  - serial: status every 5 s, plus one CSV line per committed sample
//    ("LOG,<minute>,<raw>,<pct>,<rate %/h>")

#include <TFT_eSPI.h>

// ---- calibration (sensor #1) ----
const uint16_t RAW_AIR   = 3687;  // 0 %
const uint16_t RAW_WATER = 1568;  // 100 %
const int SENSOR_PIN = A0;

// ---- behaviour ----
const float    WATER_THRESHOLD  = 30.0f;   // "water me" level, %
const uint32_t LOG_PERIOD_MS    = 60000;   // one history sample per minute
const int      HISTORY_LEN      = 2880;    // 48 h of 1-min samples
const long     RATE_WINDOW      = 360;     // fit over at most 6 h
const long     RATE_MIN_SAMPLES = 30;      // need 30 min for first estimate
const float    WATERING_JUMP    = 5.0f;    // + points over 5 min = watering

TFT_eSPI tft;

float history[HISTORY_LEN];
long  totalSamples = 0;  // absolute count of committed samples
long  segmentStart = 0;  // absolute index where current drying segment begins

float    accumPct = 0;
int      accumN = 0;
uint32_t lastLogMs = 0;
uint32_t lastStatusMs = 0;
bool     lowAlerted = false;

float histAt(long absIdx) { return history[absIdx % HISTORY_LEN]; }

uint16_t readSensorRaw() {
  const int N = 15;
  uint16_t s[N];
  for (int i = 0; i < N; i++) {
    s[i] = analogRead(SENSOR_PIN);
    delay(2);
  }
  for (int i = 1; i < N; i++) {  // insertion sort, take median
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

void commitSample(float pct) {
  history[totalSamples % HISTORY_LEN] = pct;
  totalSamples++;

  if (totalSamples >= 6 && pct - histAt(totalSamples - 6) > WATERING_JUMP) {
    segmentStart = totalSamples - 1;  // watering: restart the trend here
  }
  long floorIdx = totalSamples - min(RATE_WINDOW, (long)HISTORY_LEN);
  if (segmentStart < floorIdx) segmentStart = floorIdx;
  if (segmentStart < 0) segmentStart = 0;
}

// Least-squares slope of the current segment, in %/hour.
bool slopePerHour(float &out) {
  long n = totalSamples - segmentStart;
  if (n < RATE_MIN_SAMPLES) return false;
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (long i = segmentStart; i < totalSamples; i++) {
    double x = (double)(i - segmentStart);  // minutes
    double y = histAt(i);
    sx += x; sy += y; sxx += x * x; sxy += x * y;
  }
  double denom = (double)n * sxx - sx * sx;
  if (denom == 0) return false;
  out = (float)(((double)n * sxy - sx * sy) / denom * 60.0);
  return true;
}

void chirp() {
  tone(WIO_BUZZER, 1800, 120);
  delay(180);
  tone(WIO_BUZZER, 1400, 200);
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
    tft.drawPixel(x, yThr, TFT_RED);  // dashed threshold line
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

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  pinMode(SENSOR_PIN, INPUT);
  pinMode(WIO_BUZZER, OUTPUT);

  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Soil Moisture  #1", 70, 8);
  drawSparkline();
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
    commitSample(mean);
    drawSparkline();
    drawTrend(mean);

    float rate = 0;
    bool haveRate = slopePerHour(rate);
    Serial.print("LOG,");
    Serial.print(totalSamples);
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
      lowAlerted = false;  // hysteresis
    }
  }

  if (millis() - lastStatusMs >= 5000) {
    lastStatusMs = millis();
    Serial.print("raw=");
    Serial.print(raw);
    Serial.print("  pct=");
    Serial.println(pct, 1);
  }

  delay(1000);
}
