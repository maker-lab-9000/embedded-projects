// Soil moisture monitor for M5StickS3 — port of the Wio Terminal m5_power sketch.
// Wiring (top HAT header): sensor VCC -> 3V3_L2, GND -> GND, AOUT -> G7.
//
// FQBN: esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB
// Port: /dev/cu.usbmodem14101 (native USB CDC; opening the port resets the device)

#include <M5Unified.h>

// ---- calibration (sensor #2, see CALIBRATION.md) ----
const uint16_t RAW_AIR   = 3573;  // 0 %
const uint16_t RAW_WATER = 1392;  // 100 %
const int SENSOR_PIN = 7;         // G7, ADC1_CH6
const int SENSOR_ID  = 2;

// ---- behaviour (identical to the Wio m5_power sketch) ----
const float WATER_THRESHOLD = 30.0f;

uint32_t lastSenseMs = 0;

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

// ---------- display (240x135 landscape) ----------

void drawHeader() {
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.drawString("Soil Moisture #2", 0, 0);
}

void drawLive(float pct, uint16_t raw) {
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
}

void loop() {
  M5.update();

  if (millis() - lastSenseMs >= 1000) {
    lastSenseMs = millis();
    uint16_t raw = readSensorRaw();
    float pct = rawToPercent(raw);
    drawLive(pct, raw);
  }

  delay(25);
}
