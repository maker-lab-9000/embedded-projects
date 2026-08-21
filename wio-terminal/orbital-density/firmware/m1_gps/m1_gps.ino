// Milestone 1 — Air530 GPS bring-up on the Wio Terminal (orbital-density project).
// Reads NMEA from Serial1 (Grove UART @ 9600), echoes raw sentences and a
// TinyGPS++ parsed summary over USB serial. Validates wiring, baud, reception.
//
// FQBN: Seeeduino:samd:seeed_wio_terminal
// Air530 -> Wio UART Grove port (Serial1).

#include <TinyGPSPlus.h>

TinyGPSPlus gps;
uint32_t lastPrint = 0;

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);   // Air530 default
}

void loop() {
  while (Serial1.available()) {
    char c = Serial1.read();
    gps.encode(c);
    Serial.write(c);      // echo raw NMEA so we can see sentences
  }

  if (millis() - lastPrint >= 2000) {
    lastPrint = millis();
    Serial.print("\n== fix=");
    Serial.print(gps.location.isValid() ? "yes" : "no");
    Serial.print(" satsUsed=");
    Serial.print(gps.satellites.isValid() ? (int)gps.satellites.value() : -1);
    Serial.print(" hdop=");
    Serial.print(gps.hdop.isValid() ? gps.hdop.hdop() : -1.0, 1);
    Serial.print(" chars=");
    Serial.print(gps.charsProcessed());
    Serial.print(" sentences=");
    Serial.print(gps.sentencesWithFix());
    Serial.println(" ==");
  }
}
