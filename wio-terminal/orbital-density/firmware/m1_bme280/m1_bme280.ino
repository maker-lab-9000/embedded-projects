// Milestone 1 — BME280 (Seengreat) bring-up on Wio Terminal I2C Grove port.
// Scans I2C bus first, then reads temp / humidity / pressure every 2 seconds.
//
// Wiring: Grove I2C port (left side) → BME280 Grove connector.
// Default address 0x76 (ADDR jumper on board selects 0x76 vs 0x77).
// FQBN: Seeeduino:samd:seeed_wio_terminal

#include <Wire.h>

void i2cScan() {
  Serial.println("--- I2C scan ---");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  0x%02X found\n", addr);
      found++;
    }
  }
  Serial.printf("--- %d device(s) ---\n", found);
}

// BME280 registers — address auto-detected at boot
uint8_t BME_ADDR = 0x76;
#define REG_ID       0xD0
#define REG_CTRL_HUM 0xF2
#define REG_CTRL_MEAS 0xF4
#define REG_CONFIG   0xF5
#define REG_CALIB00  0x88
#define REG_CALIB26  0xE1
#define REG_DATA     0xF7

uint16_t dig_T1; int16_t dig_T2, dig_T3;
uint16_t dig_P1; int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
uint8_t  dig_H1; int16_t dig_H2; uint8_t dig_H3; int16_t dig_H4, dig_H5; int8_t dig_H6;
int32_t t_fine;

uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(BME_ADDR);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(BME_ADDR, 1);
  return Wire.read();
}

void readRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(BME_ADDR);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(BME_ADDR, (int)len);
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
}

void readCalibration() {
  uint8_t c[26];
  readRegs(REG_CALIB00, c, 26);
  dig_T1 = c[0] | (c[1] << 8);
  dig_T2 = c[2] | (c[3] << 8);
  dig_T3 = c[4] | (c[5] << 8);
  dig_P1 = c[6] | (c[7] << 8);
  dig_P2 = c[8] | (c[9] << 8);
  dig_P3 = c[10] | (c[11] << 8);
  dig_P4 = c[12] | (c[13] << 8);
  dig_P5 = c[14] | (c[15] << 8);
  dig_P6 = c[16] | (c[17] << 8);
  dig_P7 = c[18] | (c[19] << 8);
  dig_P8 = c[20] | (c[21] << 8);
  dig_P9 = c[22] | (c[23] << 8);
  dig_H1 = c[25]; // 0xA1

  uint8_t h[7];
  readRegs(REG_CALIB26, h, 7);
  dig_H2 = h[0] | (h[1] << 8);
  dig_H3 = h[2];
  dig_H4 = (h[3] << 4) | (h[4] & 0x0F);
  dig_H5 = (h[5] << 4) | (h[4] >> 4);
  dig_H6 = h[6];
}

float compensateTemp(int32_t adc) {
  int32_t v1 = ((((adc >> 3) - ((int32_t)dig_T1 << 1))) * (int32_t)dig_T2) >> 11;
  int32_t v2 = (((((adc >> 4) - (int32_t)dig_T1) * ((adc >> 4) - (int32_t)dig_T1)) >> 12) * (int32_t)dig_T3) >> 14;
  t_fine = v1 + v2;
  return (t_fine * 5 + 128) / 256 / 100.0f;
}

float compensatePressure(int32_t adc) {
  int64_t v1 = (int64_t)t_fine - 128000;
  int64_t v2 = v1 * v1 * (int64_t)dig_P6 + ((v1 * (int64_t)dig_P5) << 17) + ((int64_t)dig_P4 << 35);
  v1 = ((v1 * v1 * (int64_t)dig_P3) >> 8) + ((v1 * (int64_t)dig_P2) << 12);
  v1 = (((int64_t)1 << 47) + v1) * (int64_t)dig_P1 >> 33;
  if (v1 == 0) return 0;
  int64_t p = 1048576 - adc;
  p = (((p << 31) - v2) * 3125) / v1;
  v1 = ((int64_t)dig_P9 * (p >> 13) * (p >> 13)) >> 25;
  v2 = ((int64_t)dig_P8 * p) >> 19;
  return ((p + v1 + v2) >> 8) / 256.0f / 100.0f; // hPa
}

float compensateHumidity(int32_t adc) {
  int32_t v = t_fine - 76800;
  v = (((adc << 14) - ((int32_t)dig_H4 << 20) - ((int32_t)dig_H5 * v)) + 16384) >> 15;
  v = v * (((((((v * (int32_t)dig_H6) >> 10) * (((v * (int32_t)dig_H3) >> 11) + 32768)) >> 10) + 2097152) * (int32_t)dig_H2 + 8192) >> 14);
  v = v - (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)dig_H1) >> 4);
  if (v < 0) v = 0;
  if (v > 419430400) v = 419430400;
  return (v >> 12) / 1024.0f;
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  delay(3000);
  Serial.println("BME280 bring-up");

  i2cScan();

  // Try 0x76 first, then 0x77
  static const uint8_t addrs[] = {0x76, 0x77};
  bool ok = false;
  for (int i = 0; i < 2; i++) {
    BME_ADDR = addrs[i];
    uint8_t id = readReg(REG_ID);
    Serial.printf("0x%02X -> chip ID 0x%02X\n", addrs[i], id);
    if (id == 0x60 || id == 0x58) {
      Serial.printf("Found %s at 0x%02X\n", id == 0x60 ? "BME280" : "BMP280", addrs[i]);
      ok = true;
      break;
    }
  }
  if (!ok) {
    Serial.println("ERROR: no BME280/BMP280 at 0x76 or 0x77");
    return;
  }

  readCalibration();

  // Oversampling: temp x1, pressure x1, humidity x1, normal mode
  Wire.beginTransmission(BME_ADDR);
  Wire.write(REG_CTRL_HUM);
  Wire.write(0x01); // humidity x1
  Wire.endTransmission();

  Wire.beginTransmission(BME_ADDR);
  Wire.write(REG_CTRL_MEAS);
  Wire.write(0x27); // temp x1, press x1, normal mode
  Wire.endTransmission();

  Wire.beginTransmission(BME_ADDR);
  Wire.write(REG_CONFIG);
  Wire.write(0xA0); // standby 1000ms, filter off
  Wire.endTransmission();

  Serial.println("BME280 configured — reading every 2s");
}

bool configured = false;
unsigned long loopCount = 0;

void configureBME() {
  readCalibration();
  Wire.beginTransmission(BME_ADDR);
  Wire.write(REG_CTRL_HUM);
  Wire.write(0x01);
  Wire.endTransmission();
  Wire.beginTransmission(BME_ADDR);
  Wire.write(REG_CTRL_MEAS);
  Wire.write(0x27);
  Wire.endTransmission();
  Wire.beginTransmission(BME_ADDR);
  Wire.write(REG_CONFIG);
  Wire.write(0xA0);
  Wire.endTransmission();
  configured = true;
  Serial.printf("Configured BME280 at 0x%02X\n", BME_ADDR);
}

void loop() {
  if (!configured || loopCount % 5 == 0) {
    i2cScan();
    uint8_t id76, id77;
    BME_ADDR = 0x76; id76 = readReg(REG_ID);
    BME_ADDR = 0x77; id77 = readReg(REG_ID);
    Serial.printf("chip ID @ 0x76=0x%02X  0x77=0x%02X\n", id76, id77);
    if (id76 == 0x60 || id76 == 0x58) { BME_ADDR = 0x76; if (!configured) configureBME(); }
    else if (id77 == 0x60 || id77 == 0x58) { BME_ADDR = 0x77; if (!configured) configureBME(); }
  }
  loopCount++;

  uint8_t buf[8];
  readRegs(REG_DATA, buf, 8);
  Serial.printf("reg[F7..FE]: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7]);

  int32_t pRaw = ((int32_t)buf[0] << 12) | ((int32_t)buf[1] << 4) | (buf[2] >> 4);
  int32_t tRaw = ((int32_t)buf[3] << 12) | ((int32_t)buf[4] << 4) | (buf[5] >> 4);
  int32_t hRaw = ((int32_t)buf[6] << 8) | buf[7];

  float temp = compensateTemp(tRaw);
  float pres = compensatePressure(pRaw);
  float hum  = compensateHumidity(hRaw);

  Serial.printf("raw T=%ld P=%ld H=%ld | T=%.2fC  H=%.1f%%  P=%.1fhPa\n",
                 tRaw, pRaw, hRaw, temp, hum, pres);
  delay(2000);
}
