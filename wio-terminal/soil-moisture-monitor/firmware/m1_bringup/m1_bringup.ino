// Milestone 1 — sensor bring-up.
// One capacitive soil moisture sensor v1.2 on A0 (physical pin 13 of the
// 40-pin header). Prints the median raw ADC value and its voltage over
// serial so we can validate wiring and the dry-air vs in-water delta.
//
// Expected behaviour (3.3 V supply, 12-bit ADC):
//   probe in dry air : high reading, roughly 2600-3100
//   probe in water   : low reading,  roughly 1300-1700
// Exact numbers vary per board; a large, stable delta is what matters.
// If the reading barely moves between air and water, that sensor unit
// likely has a 555 that needs 5 V (see PLAN.md, clone caveat).

const int SENSOR_PIN = A0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000) {}  // wait for monitor, boot anyway
  analogReadResolution(12);              // SAMD51 ADC: 0..4095
  pinMode(SENSOR_PIN, INPUT);
}

// Median of N samples rejects ADC jitter and EMI spikes far better
// than a mean for this sensor.
uint16_t readSensorRaw() {
  const int N = 15;
  uint16_t s[N];
  for (int i = 0; i < N; i++) {
    s[i] = analogRead(SENSOR_PIN);
    delay(2);
  }
  for (int i = 1; i < N; i++) {  // insertion sort
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

void loop() {
  uint16_t raw = readSensorRaw();
  float volts = raw * 3.3f / 4095.0f;
  Serial.print("raw=");
  Serial.print(raw);
  Serial.print("  V=");
  Serial.println(volts, 3);
  delay(500);
}
