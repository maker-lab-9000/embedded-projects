// Milestone 1 — MMC5603 magnetometer bring-up (serial only).
// WIRING: hub socket -> Adafruit #4528 Grove-to-STEMMA QT cable -> MMC5603 (I2C 0x30), on the arm.
// (With a single #4528 cable: chain it from the TSL2591's second QT connector instead.)
// Continuous mode at 10 Hz so getEvent() is a plain register read — the Adafruit library
// busy-waits (delay(5) loop) in one-shot mode, which the main firmware cannot afford.
// Auto set/reset (CTRL0 bit 5, Auto_SR_en) is enabled with a raw register write: the datasheet
// recommends it, the raw bridge offset is specified at up to ±1 G (±100 uT) per axis without
// it, and the Adafruit library never sets it (its reset() even leaves the sensor in RESET
// polarity, output = -H + offset). Seen 2026-09-03: a steady 173 uT "field" that was offset.
// FQBN: Seeeduino:samd:seeed_wio_terminal

#include <Wire.h>
#include <Adafruit_MMC56x3.h>

Adafruit_MMC5603 mmc;
bool magOk = false;
uint32_t lastPrint = 0, lastProbe = 0;

const uint8_t MMC_CTRL0_REG = 0x1B, MMC_CTRL0_AUTO_SR_EN = 0x20;

// Auto_SR_en is a level bit in the write-only CTRL0. The library's setContinuousMode() writes
// CTRL0 = 0x80 (Cmm_freq_en pulse), so this must run AFTER it; Cmm_freq_en self-clears, so
// writing it back as 0 here does not stop continuous mode.
bool mmcEnableAutoSetReset() {
  Wire.beginTransmission(MMC56X3_DEFAULT_ADDRESS);
  Wire.write(MMC_CTRL0_REG);
  Wire.write(MMC_CTRL0_AUTO_SR_EN);
  return Wire.endTransmission() == 0;
}

bool magInit() {
  if (!mmc.begin(MMC56X3_DEFAULT_ADDRESS, &Wire)) return false;   // checks product ID 0x10
  mmc.setDataRate(10);          // Hz
  mmc.setContinuousMode(true);
  return mmcEnableAutoSetReset();
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Wire.begin();
  Wire.setClock(100000);
  magOk = magInit();
  if (!magOk) Serial.println("MMC5603 not found at 0x30");
}

void loop() {
  if (!magOk) {
    if (millis() - lastProbe >= 5000) { lastProbe = millis(); magOk = magInit(); }
    return;
  }
  if (millis() - lastPrint >= 500) {
    lastPrint = millis();
    sensors_event_t e;
    uint32_t t0 = micros();
    bool ok = mmc.getEvent(&e);
    uint32_t dt = micros() - t0;
    if (!ok) { Serial.println("MMC5603 read failed"); magOk = false; return; }
    float x = e.magnetic.x, y = e.magnetic.y, z = e.magnetic.z;
    float total = sqrtf(x * x + y * y + z * z);
    float heading = atan2f(y, x) * 180.0f / PI;
    if (heading < 0) heading += 360.0f;
    Serial.printf("x=%7.2f y=%7.2f z=%7.2f |B|=%6.2f uT  heading=%5.1f  read=%lu us\n",
                  x, y, z, total, heading, (unsigned long)dt);
  }
}
