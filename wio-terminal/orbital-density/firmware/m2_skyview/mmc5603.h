// mmc5603.h — Adafruit MMC5603 magnetometer (I2C 0x30) at the end of the 15–30 cm
// non-magnetic arm. Continuous mode at 10 Hz so getEvent() is a plain register read (the
// library busy-waits in one-shot mode). Auto set/reset (CTRL0 Auto_SR_en) is enabled by a
// raw register write: without it the raw bridge offset is up to ±1 G (±100 uT) per axis and
// the library leaves the sensor in RESET polarity (output = -H + offset) — bring-up showed a
// steady 173 uT that was pure offset. Heading assumes the instrument is level; tilt
// compensation is future work (the Wio's built-in LIS3DHTR on Wire1 is the obvious source).
#pragma once
#include <Arduino.h>
#include <Adafruit_MMC56x3.h>
#include "ring.h"

const uint16_t MAG_RATE_HZ     = 10;
const uint32_t MAG_POLL_MS     = 100;
const uint8_t  MMC_CTRL0_REG = 0x1B, MMC_CTRL0_AUTO_SR_EN = 0x20;
// Calibration constants — set from field data (README "Magnetometer calibration", tools/mag_calib.py).
const float MAG_OFF_X = 0.0f, MAG_OFF_Y = 0.0f, MAG_OFF_Z = 0.0f;   // hard-iron offsets, uT
const float MAG_MOUNT_OFFSET_DEG = 0.0f;   // board +X axis relative to the instrument's forward direction
const float MAG_DECLINATION_DEG  = 0.0f;   // magnetic -> true north for the observing site

Adafruit_MMC5603 mmc;
bool     magOk = false;
uint32_t magLastMs = 0;
float    magX = 0, magY = 0, magZ = 0, magTotal = 0, magHeading = 0;          // latest 10 Hz sample
float    magMeanX = 0, magMeanY = 0, magMeanZ = 0, magMeanTotal = 0, magMeanHeading = 0;   // last 1 s mean
static float magAccX = 0, magAccY = 0, magAccZ = 0;
static int   magAccN = 0;
uint32_t magSampleCount = 0, magErrCount = 0;
Ring<int16_t, SENSOR_HIST_N> magTotalHist;   // |B| * 10, one sample / 5 min

static float magHeadingFrom(float x, float y) {
  float h = atan2f(y, x) * 180.0f / PI + MAG_MOUNT_OFFSET_DEG + MAG_DECLINATION_DEG;
  while (h < 0) h += 360.0f;
  while (h >= 360.0f) h -= 360.0f;
  return h;
}

// Auto_SR_en is a level bit in the write-only CTRL0; setContinuousMode() writes CTRL0 = 0x80
// (Cmm_freq_en pulse, self-clearing), so this must come after it.
static bool mmcEnableAutoSetReset() {
  Wire.beginTransmission(MMC56X3_DEFAULT_ADDRESS);
  Wire.write(MMC_CTRL0_REG);
  Wire.write(MMC_CTRL0_AUTO_SR_EN);
  return Wire.endTransmission() == 0;
}

// Probe (product ID 0x10) + configure. Safe to call again; returns magOk.
bool magInit() {
  if (!mmc.begin(MMC56X3_DEFAULT_ADDRESS, &Wire)) { magOk = false; return false; }
  mmc.setDataRate(MAG_RATE_HZ);
  mmc.setContinuousMode(true);
  magOk = mmcEnableAutoSetReset();
  return magOk;
}

// Call every loop() iteration; touches the bus at most once per MAG_POLL_MS.
void magPoll() {
  if (!magOk) return;
  uint32_t now = millis();
  if (now - magLastMs < MAG_POLL_MS) return;
  magLastMs = now;
  sensors_event_t e;
  if (!mmc.getEvent(&e)) { magErrCount++; magOk = false; return; }
  magX = e.magnetic.x - MAG_OFF_X;
  magY = e.magnetic.y - MAG_OFF_Y;
  magZ = e.magnetic.z - MAG_OFF_Z;
  magTotal   = sqrtf(magX * magX + magY * magY + magZ * magZ);
  magHeading = magHeadingFrom(magX, magY);
  magAccX += magX; magAccY += magY; magAccZ += magZ; magAccN++;
  magSampleCount++;
}

// Call once per second, before the observation is assembled: folds the last second's
// samples into the means and restarts the accumulator.
void magRollSecond() {
  if (magAccN == 0) return;
  magMeanX = magAccX / magAccN; magMeanY = magAccY / magAccN; magMeanZ = magAccZ / magAccN;
  magMeanTotal   = sqrtf(magMeanX * magMeanX + magMeanY * magMeanY + magMeanZ * magMeanZ);
  magMeanHeading = magHeadingFrom(magMeanX, magMeanY);
  magAccX = magAccY = magAccZ = 0; magAccN = 0;
}

// Call every 5 min (existing HIST block).
void magPushHist() { if (magOk) magTotalHist.push((int16_t)(magMeanTotal * 10.0f + 0.5f)); }
