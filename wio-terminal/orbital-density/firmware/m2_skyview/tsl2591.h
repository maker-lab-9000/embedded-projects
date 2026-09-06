// tsl2591.h — register-level, NON-BLOCKING driver for the Adafruit TSL2591 (I2C 0x29).
//
// Why not Adafruit_TSL2591? Its getFullLuminosity() sleeps 120 ms per integration step
// (720 ms at the 600 ms integration a dark sky needs). The Wio's 256-byte UART buffer
// overflows in ~22 ms at 115200 baud, so that would drop NMEA every second. Here the ALS
// runs continuously and tslPoll() only reads registers when a new result is due.
//
// Bus: Wire @ 100 kHz (shared with an SMBus MLX90614). Supply 3.3 V.
// Datasheet: command byte 0xA0|reg; ENABLE 0x00 (PON 0x01, AEN 0x02); CONTROL 0x01
// (AGAIN bits 5:4, ATIME bits 2:0); ID 0x12 = 0x50; STATUS 0x13 bit0 AVALID;
// C0DATAL..C1DATAH 0x14..0x17. Full scale 36863 at 100 ms, 65535 otherwise.
#pragma once
#include <Arduino.h>
#include <Wire.h>

const uint8_t TSL_ADDR        = 0x29;
const uint8_t TSL_CMD         = 0xA0;
const uint8_t TSL_REG_ENABLE  = 0x00;
const uint8_t TSL_REG_CONTROL = 0x01;
const uint8_t TSL_REG_ID      = 0x12;
const uint8_t TSL_REG_STATUS  = 0x13;
const uint8_t TSL_REG_C0DATAL = 0x14;
const uint8_t TSL_EN_PON      = 0x01;
const uint8_t TSL_EN_AEN      = 0x02;

const uint8_t  TSL_GAIN_BITS[4] = {0x00, 0x10, 0x20, 0x30};
const float    TSL_GAIN_X[4]    = {1.0f, 25.0f, 428.0f, 9876.0f};
const char*    TSL_GAIN_NAME[4] = {"low", "med", "high", "max"};
const uint16_t TSL_INTEG_MS[6]  = {100, 200, 300, 400, 500, 600};
const float    TSL_LUX_DF       = 408.0f;   // Adafruit's device factor

bool     tslOk = false;
uint8_t  tslGainIdx  = 1;      // start MED / 300 ms: auto-range moves from here within seconds
uint8_t  tslIntegIdx = 2;
uint16_t tslIntegMs  = 300;
uint16_t tslFull = 0, tslIr = 0;
float    tslLux = NAN;
bool     tslSat = false;
bool     tslSettling = true;   // discard the first result after any settings change
uint32_t tslLastMs = 0;        // millis() of the last register read
uint32_t tslSampleCount = 0;
uint32_t tslErrCount = 0;

static bool tslWrite8(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(TSL_ADDR);
  Wire.write(TSL_CMD | reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool tslRead(uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(TSL_ADDR);
  Wire.write(TSL_CMD | reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(TSL_ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static void tslApplySettings() {
  tslIntegMs = TSL_INTEG_MS[tslIntegIdx];
  tslWrite8(TSL_REG_CONTROL, TSL_GAIN_BITS[tslGainIdx] | tslIntegIdx);
  tslSettling = true;
  tslLastMs = millis();
}

const char* tslGainName() { return TSL_GAIN_NAME[tslGainIdx]; }

// Probe + configure. Safe to call again later (re-probe); returns tslOk.
bool tslInit() {
  uint8_t id = 0;
  if (!tslRead(TSL_REG_ID, &id, 1) || id != 0x50) { tslOk = false; return false; }
  tslWrite8(TSL_REG_ENABLE, TSL_EN_PON);
  tslApplySettings();
  tslWrite8(TSL_REG_ENABLE, TSL_EN_PON | TSL_EN_AEN);   // ALS runs continuously from here
  tslOk = true;
  return true;
}

static uint16_t tslFullScale() { return tslIntegIdx == 0 ? 36863 : 65535; }

// Auto-range: less sensitivity when near full scale, more when counts are tiny.
// Returns true if settings changed (the next result is then discarded).
static bool tslAutoRange(uint16_t ch0) {
  uint32_t fs = tslFullScale();
  if (tslSat || ch0 > fs * 9 / 10) {
    if (tslGainIdx > 0)  { tslGainIdx--;  tslApplySettings(); return true; }
    if (tslIntegIdx > 0) { tslIntegIdx--; tslApplySettings(); return true; }
    return false;   // already LOW / 100 ms: genuine saturation, tslSat stays set
  }
  if (ch0 < 200) {
    if (tslIntegIdx < 5) { tslIntegIdx = 5; tslApplySettings(); return true; }
    if (tslGainIdx < 3)  { tslGainIdx++;  tslApplySettings(); return true; }
  }
  return false;
}

// Call every loop() iteration. Never blocks: at most one 1-byte status read and one
// 4-byte burst per integration period.
void tslPoll() {
  if (!tslOk) return;
  uint32_t now = millis();
  if (now - tslLastMs < (uint32_t)tslIntegMs + 20) return;
  uint8_t st = 0;
  if (!tslRead(TSL_REG_STATUS, &st, 1)) { tslErrCount++; tslOk = false; return; }
  if (!(st & 0x01)) return;                       // AVALID not yet set (first cycle after enable)
  uint8_t d[4];
  if (!tslRead(TSL_REG_C0DATAL, d, 4)) { tslErrCount++; tslOk = false; return; }
  tslLastMs = now;
  uint16_t ch0 = d[0] | (d[1] << 8);              // full spectrum, read first (one burst)
  uint16_t ch1 = d[2] | (d[3] << 8);              // infrared
  if (tslSettling) { tslSettling = false; return; }
  tslFull = ch0; tslIr = ch1;
  tslSat  = (ch0 >= tslFullScale()) || (ch1 >= tslFullScale());
  if (tslSat || ch0 == 0) tslLux = NAN;
  else {
    float cpl = ((float)tslIntegMs * TSL_GAIN_X[tslGainIdx]) / TSL_LUX_DF;
    tslLux = ((float)ch0 - (float)ch1) * (1.0f - (float)ch1 / (float)ch0) / cpl;
    if (tslLux < 0) tslLux = 0;
  }
  tslSampleCount++;
  tslAutoRange(ch0);
}
