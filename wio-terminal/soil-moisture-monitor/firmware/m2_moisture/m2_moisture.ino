// Milestone 2 — calibrated moisture % on the LCD.
// Sensor #1 on A0 (physical pin 13). Calibration anchors measured
// 2026-08-16 (see CALIBRATION.md). Serial still prints raw + % for logging.

#include <TFT_eSPI.h>

const uint16_t RAW_AIR   = 3687;  // probe in dry air  -> 0 %
const uint16_t RAW_WATER = 1568;  // probe in water    -> 100 %

const int SENSOR_PIN = A0;

TFT_eSPI tft;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  pinMode(SENSOR_PIN, INPUT);

  tft.begin();
  tft.setRotation(3);  // landscape, USB-C on the left
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextSize(3);
  tft.drawString("Soil Moisture", 60, 16);
}

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

void loop() {
  uint16_t raw = readSensorRaw();
  float pct = rawToPercent(raw);

  uint16_t color = pct < 30 ? TFT_RED : (pct < 50 ? TFT_YELLOW : TFT_GREEN);

  // Big percentage, fixed-width so old digits are overwritten
  char buf[8];
  snprintf(buf, sizeof(buf), "%3d%%", (int)(pct + 0.5f));
  tft.setTextSize(7);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(buf, 76, 70);

  // Bar meter
  const int bx = 20, by = 150, bw = 280, bh = 24;
  int fill = (int)(bw * pct / 100.0f);
  tft.drawRect(bx - 1, by - 1, bw + 2, bh + 2, TFT_LIGHTGREY);
  tft.fillRect(bx, by, fill, bh, color);
  tft.fillRect(bx + fill, by, bw - fill, bh, TFT_BLACK);

  // Raw diagnostics line
  char diag[32];
  snprintf(diag, sizeof(diag), "raw %4u   %.2fV", raw, raw * 3.3f / 4095.0f);
  tft.setTextSize(2);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(diag, 60, 200);

  Serial.print("raw=");
  Serial.print(raw);
  Serial.print("  pct=");
  Serial.println(pct, 1);

  delay(1000);
}
