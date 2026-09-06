// Milestone 1 — TSL2591 optical sky sensor bring-up (serial only).
// WIRING: Grove I2C Hub on the LEFT Grove port -> Adafruit #4528 Grove-to-STEMMA QT cable -> TSL2591.
// Prints raw full/IR counts, gain, integration time and lux once per second; auto-range on.
// FQBN: Seeeduino:samd:seeed_wio_terminal

#include <Wire.h>
#include "tsl2591.h"

uint32_t lastPrint = 0, lastProbe = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Wire.begin();
  Wire.setClock(100000);   // MLX90614 shares this bus: SMBus, 100 kHz max
  if (!tslInit()) Serial.println("TSL2591 not found at 0x29 (ID != 0x50)");
}

void loop() {
  if (!tslOk && millis() - lastProbe >= 5000) { lastProbe = millis(); tslInit(); }
  tslPoll();
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    if (!tslOk) { Serial.println("TSL2591: not present"); return; }
    unsigned vis = tslFull >= tslIr ? tslFull - tslIr : 0;
    Serial.printf("full=%5u ir=%5u vis=%5u gain=%-4s integ=%3u ms lux=",
                  tslFull, tslIr, vis, tslGainName(), tslIntegMs);
    if (isnan(tslLux)) Serial.print(tslSat ? "SAT" : "--");
    else Serial.printf("%.4f", tslLux);
    Serial.printf("  n=%lu err=%lu\n", (unsigned long)tslSampleCount, (unsigned long)tslErrCount);
  }
}
