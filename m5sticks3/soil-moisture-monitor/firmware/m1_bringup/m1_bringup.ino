// Milestone 1 — bring-up: raw ADC + millivolts over USB serial.
// Wiring (top HAT header): sensor VCC -> 3V3_L2, GND -> GND, AOUT -> G7.
// Validates: 3V3_L2 rail is live, wiring, air-vs-water delta, no clipping.
//
// FQBN: esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB
// Port: /dev/cu.usbmodem14101 (native USB CDC; factory descriptor "M5Stack UiFlow 2_0")

#include <M5Unified.h>

const int SENSOR_PIN = 7;  // G7, ADC1_CH6

uint16_t readSensorRaw() {
  const int N = 15;
  uint16_t s[N];
  for (int i = 0; i < N; i++) {
    s[i] = analogRead(SENSOR_PIN);
    delay(2);
  }
  for (int i = 1; i < N; i++) {  // insertion sort -> median
    uint16_t v = s[i];
    int j = i - 1;
    while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; }
    s[j + 1] = v;
  }
  return s[N / 2];
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);  // powers rails, inits display
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetPinAttenuation(SENSOR_PIN, ADC_11db);  // usable range ~0-3.1 V
  M5.Display.setRotation(1);
  M5.Display.setTextSize(2);
  M5.Display.drawString("m1 bringup - see serial", 4, 60);
}

void loop() {
  uint16_t raw = readSensorRaw();
  uint32_t mv = analogReadMilliVolts(SENSOR_PIN);
  Serial.printf("raw=%4u  mv=%4lu\n", raw, (unsigned long)mv);
  delay(1000);
}
