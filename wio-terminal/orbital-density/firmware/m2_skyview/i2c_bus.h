// i2c_bus.h — shared helpers for the sensor hub on the LEFT Grove port (Wire = SERCOM3, PA16/PA17).
// Bus members: 0x29 TSL2591, 0x30 MMC5603, 0x55 BQ27441 (battery chassis), 0x5A MLX90614, 0x77 BME280.
// 100 kHz is a hard limit: the MLX90614 is an SMBus device. Do not raise it for the others.
#pragma once
#include <Arduino.h>
#include <Wire.h>

const uint32_t I2C_CLOCK_HZ   = 100000;
const uint32_t I2C_REPROBE_MS = 30000;   // retry xxxInit() for missing sensors this often (hot-plug)

uint8_t i2cFound[16];
uint8_t i2cFoundCount = 0;

bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Scan 0x08..0x77 once (≈25 ms at 100 kHz — setup() only, never from loop()).
void i2cScan() {
  i2cFoundCount = 0;
  for (uint8_t a = 0x08; a <= 0x77; a++)
    if (i2cPresent(a) && i2cFoundCount < sizeof(i2cFound)) i2cFound[i2cFoundCount++] = a;
  Serial.print("I2C scan:");
  for (uint8_t i = 0; i < i2cFoundCount; i++) Serial.printf(" 0x%02X", i2cFound[i]);
  Serial.printf(" (%u devices)\n", i2cFoundCount);
}
