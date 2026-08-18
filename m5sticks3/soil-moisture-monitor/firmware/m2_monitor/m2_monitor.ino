// Soil moisture monitor for M5StickS3 — port of the Wio Terminal m5_power sketch.
// Wiring (top HAT header): sensor VCC -> 3V3_L2, GND -> GND, AOUT -> G7.
//
// FQBN: esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB
// Port: /dev/cu.usbmodem14101 (native USB CDC; opening the port resets the device)

#include <M5Unified.h>
#include <LittleFS.h>

// ---- calibration (sensor #2, see CALIBRATION.md) ----
const uint16_t RAW_AIR   = 3573;  // 0 %
const uint16_t RAW_WATER = 1392;  // 100 %
const int SENSOR_PIN = 7;         // G7, ADC1_CH6
const int SENSOR_ID  = 2;

// ---- behaviour (identical to the Wio m5_power sketch) ----
const float    WATER_THRESHOLD  = 30.0f;
const uint32_t LOG_PERIOD_MS    = 60000;
const int      HISTORY_LEN      = 2880;    // 48 h of 1-min samples
const long     RATE_WINDOW      = 360;     // fit over at most 6 h
const long     RATE_MIN_SAMPLES = 30;
const float    WATERING_JUMP    = 5.0f;

const char*    LOG_PATH  = "/soil.csv";
const char*    OLD_PATH  = "/soil.old.csv";
const uint32_t ROTATE_AT_BYTES    = 1500000;  // ~7 weeks per file
const uint32_t RESTORE_TAIL_BYTES = 90000;

bool fsOk = false;
bool screenOn = true;
bool keyBFired = false;

float history[HISTORY_LEN];
long  totalSamples = 0;
long  segmentStart = 0;
int   bootId = 1;
long  minuteIdx = 0;
float accumPct = 0;
int   accumN = 0;
float lastMean = 0;
uint32_t lastLogMs = 0;
bool  lowAlerted = false;

uint32_t lastSenseMs = 0;

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
    while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; }
    s[j + 1] = v;
  }
  return s[N / 2];
}

float rawToPercent(uint16_t raw) {
  float pct = 100.0f * (float)(RAW_AIR - raw) / (float)(RAW_AIR - RAW_WATER);
  return constrain(pct, 0.0f, 100.0f);
}

// ---------- filesystem ----------

bool initFs() {
  fsOk = LittleFS.begin(true);  // true = format on first mount
  return fsOk;
}

void rotateIfNeeded() {
  File f = LittleFS.open(LOG_PATH, "r");
  if (!f) return;
  uint32_t size = f.size();
  f.close();
  if (size < ROTATE_AT_BYTES) return;
  LittleFS.remove(OLD_PATH);
  LittleFS.rename(LOG_PATH, OLD_PATH);
  File nf = LittleFS.open(LOG_PATH, "w");
  if (nf) {
    nf.println("boot,minute,sensor,raw,pct");
    nf.close();
  }
}

bool appendLog(uint16_t raw, float pct) {
  File f = LittleFS.open(LOG_PATH, "a");
  if (!f) return false;
  f.printf("%d,%ld,%d,%u,%.2f\n", bootId, minuteIdx, SENSOR_ID, raw, pct);
  f.close();
  rotateIfNeeded();
  return true;
}

void restoreHistory() {
  if (!fsOk || !LittleFS.exists(LOG_PATH)) {
    if (fsOk) {
      File f = LittleFS.open(LOG_PATH, "w");
      if (f) { f.println("boot,minute,sensor,raw,pct"); f.close(); }
    }
    return;
  }
  File f = LittleFS.open(LOG_PATH, "r");
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
  Serial.printf("RESTORED,%ld,boot=%d\n", totalSamples, bootId);
}

void handleSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd != "DUMP") return;
  for (const char* p : {OLD_PATH, LOG_PATH}) {
    File f = LittleFS.open(p, "r");
    if (!f) continue;
    while (f.available()) Serial.write(f.read());
    f.close();
  }
  Serial.println("#DUMP_END");
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

// ---------- display (240x135 landscape) ----------

const int SPARK_X = 2, SPARK_Y = 64, SPARK_W = 236, SPARK_H = 44;

int sparkY(float pct) {
  return SPARK_Y + SPARK_H - 2 - (int)((SPARK_H - 4) * pct / 100.0f);
}

void drawSparkline() {
  if (!screenOn) return;
  M5.Display.fillRect(SPARK_X, SPARK_Y, SPARK_W, SPARK_H, TFT_BLACK);
  M5.Display.drawRect(SPARK_X, SPARK_Y, SPARK_W, SPARK_H, TFT_DARKGREY);
  int yThr = sparkY(WATER_THRESHOLD);
  for (int x = SPARK_X + 2; x < SPARK_X + SPARK_W - 2; x += 6) {
    M5.Display.drawPixel(x, yThr, TFT_RED);
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
    if (prevX >= 0) M5.Display.drawLine(prevX, prevY, x, y, TFT_CYAN);
    prevX = x; prevY = y;
  }
}

void drawTrend(float pct) {
  if (!screenOn) return;
  char line[32];
  M5.Display.setTextSize(2);

  float rate;
  bool haveRate = slopePerHour(rate);
  if (!haveRate) {
    long have = totalSamples - segmentStart;
    snprintf(line, sizeof(line), "%2ld/%ldm    ", have, RATE_MIN_SAMPLES);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.drawString(line, 104, 24);
  } else {
    snprintf(line, sizeof(line), "%+5.2f %%/h ", rate);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.drawString(line, 104, 24);
  }

  // Dry soil needs no rate estimate: WATER NOW! overrides every other ETA
  // state, warm-up included. totalSamples guard avoids a false alarm on a
  // first boot with no reading yet (pct defaults to 0).
  if (pct <= WATER_THRESHOLD && totalSamples > 0) {
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.drawString("WATER NOW! ", 104, 44);
  } else if (!haveRate) {
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.drawString("ETA --     ", 104, 44);
  } else if (rate >= -0.005f) {
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.drawString("not drying ", 104, 44);
  } else {
    float hours = (pct - WATER_THRESHOLD) / -rate;
    if (hours > 99 * 24) {
      snprintf(line, sizeof(line), ">99 days   ");
    } else {
      int d = (int)(hours / 24), h = (int)hours % 24;
      snprintf(line, sizeof(line), "~%dd %02dh   ", d, h);
    }
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.drawString(line, 104, 44);
  }
}

void drawHeader() {
  if (!screenOn) return;
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.drawString("Soil Moisture #2", 0, 0);
}

void drawFsBadge() {
  if (!screenOn) return;
  M5.Display.setTextSize(2);
  if (fsOk) {
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.drawString("FS ", 204, 0);
  } else {
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.drawString("FS!", 204, 0);
  }
}

void drawLive(float pct, uint16_t raw) {
  if (!screenOn) return;
  uint16_t color = pct < WATER_THRESHOLD ? TFT_RED
                 : (pct < 50 ? TFT_YELLOW : TFT_GREEN);
  char buf[8];
  snprintf(buf, sizeof(buf), "%3d%%", (int)(pct + 0.5f));
  M5.Display.setTextSize(4);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.drawString(buf, 0, 20);

  char diag[48];
  uint32_t upMin = millis() / 60000;
  int bat = M5.Power.getBatteryLevel();  // 0-100, or <0 if unknown
  if (bat >= 0) {
    snprintf(diag, sizeof(diag), "raw %4u  bat %3d%%  up %lu:%02lu   ", raw,
             bat, (unsigned long)(upMin / 60), (unsigned long)(upMin % 60));
  } else {
    snprintf(diag, sizeof(diag), "raw %4u  bat --  up %lu:%02lu   ", raw,
             (unsigned long)(upMin / 60), (unsigned long)(upMin % 60));
  }
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.drawString(diag, 4, 124);
}

// ---------- controls ----------

void chirp() {
  M5.Speaker.tone(1800, 120);
  delay(180);
  M5.Speaker.tone(1400, 200);
}

void setScreen(bool on) {
  screenOn = on;
  if (on) {
    M5.Display.wakeup();
    M5.Display.setBrightness(80);
    M5.Display.fillScreen(TFT_BLACK);
    drawHeader();
    drawFsBadge();
    drawSparkline();
    drawTrend(lastMean);
  } else {
    M5.Display.setBrightness(0);
    M5.Display.sleep();
  }
}

// After moving the probe: forget the old spot's history, mark it in the log.
void restartTrends() {
  totalSamples = 0;
  segmentStart = 0;
  accumPct = 0;
  accumN = 0;
  if (fsOk) {
    File f = LittleFS.open(LOG_PATH, "a");
    if (f) { f.printf("#RESET,%ld\n", minuteIdx); f.close(); }
  }
  Serial.printf("TRENDS_RESET,%ld\n", minuteIdx);
  chirp();
  drawSparkline();
  drawTrend(lastMean);
}

void pollButtons() {
  if (M5.BtnA.wasClicked()) setScreen(!screenOn);

  if (M5.BtnB.pressedFor(3000)) {
    if (!keyBFired) { keyBFired = true; restartTrends(); }
  } else if (M5.BtnB.wasReleased()) {
    keyBFired = false;
  }
}

// ---------- main ----------

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetPinAttenuation(SENSOR_PIN, ADC_11db);

  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  drawHeader();

  initFs();
  restoreHistory();
  drawFsBadge();
  drawSparkline();
  if (totalSamples > 0) lastMean = histAt(totalSamples - 1);
  drawTrend(lastMean);

  lastLogMs = millis();
}

void loop() {
  M5.update();
  handleSerial();
  pollButtons();

  if (millis() - lastSenseMs >= 1000) {
    lastSenseMs = millis();
    uint16_t raw = readSensorRaw();
    float pct = rawToPercent(raw);
    drawLive(pct, raw);
    accumPct += pct;
    accumN++;

    if (millis() - lastLogMs >= LOG_PERIOD_MS && accumN > 0) {
      lastLogMs += LOG_PERIOD_MS;
      float mean = accumPct / accumN;
      accumPct = 0;
      accumN = 0;
      lastMean = mean;

      commitToHistory(mean);

      if (!fsOk) initFs();
      if (fsOk && !appendLog(raw, mean)) fsOk = false;
      drawFsBadge();
      minuteIdx++;

      drawSparkline();
      drawTrend(mean);

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
