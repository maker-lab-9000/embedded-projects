// Milestone 1 — Air530 GPS bring-up on the Wio Terminal (orbital-density project).
// Reads NMEA from Serial1 @ 9600, echoes raw sentences + a TinyGPS++ summary.
//
// WIRING: the Wio's hardware UART (Serial1) is on the 40-pin HEADER, not a Grove
// port. Connect the GPS to:
//   GPS TX  -> header pin 10 (BCM15 / RXD / Serial1 RX)
//   GPS RX  -> header pin  8 (BCM14 / TXD)          [optional]
//   GPS VCC -> header pin  1 (3V3)  |  GPS GND -> header pin 6 (GND)
// (The D0/D1 Grove port has no usable hardware UART.)
//
// FQBN: Seeeduino:samd:seeed_wio_terminal

#include <TinyGPSPlus.h>

TinyGPSPlus gps;
uint32_t lastPrint = 0;

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);
}

void loop() {
  while (Serial1.available()) {
    char c = Serial1.read();
    gps.encode(c);
    Serial.write(c);
  }
  if (millis() - lastPrint >= 2000) {
    lastPrint = millis();
    Serial.print("\n== chars="); Serial.print(gps.charsProcessed());
    Serial.print(" fix="); Serial.print(gps.location.isValid() ? "yes" : "no");
    Serial.print(" satsUsed="); Serial.print(gps.satellites.isValid() ? (int)gps.satellites.value() : -1);
    Serial.println(" ==");
  }
}
