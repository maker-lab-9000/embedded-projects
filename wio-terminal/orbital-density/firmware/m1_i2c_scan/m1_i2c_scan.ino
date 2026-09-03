// Milestone 1 — I2C bus scan + GNSS UART check for the sky-observatory re-cabling.
//
// WIRING (Wio Terminal in the 650 mAh battery chassis):
//   chassis socket labelled RX TX (beside the USB-C) -> Grove cable -> Air530Z GNSS  (= header 10/8 = Serial1)
//   the four IO* sockets look identical but have no UART: a GPS there is silent on Serial1
//   chassis Grove I2C socket  -> spare (GY-906 / MLX90614 goes here once its header is soldered)
//   LEFT Grove port (SDA/SCL) -> Grove cable -> Grove I2C Hub (passive, 4 sockets in parallel)
//     hub socket -> Grove cable -> BME280
//     hub socket -> Adafruit #4528 Grove-to-STEMMA QT cable -> TSL2591
//     hub socket -> Adafruit #4528 Grove-to-STEMMA QT cable -> MMC5603 (or chain it from the TSL2591)
// Expected: 0x29 TSL2591  0x30 MMC5603  0x55 BQ27441 (chassis)  0x77 BME280  (0x5A MLX90614 later),
// plus a non-zero NMEA sentence count from Serial1 every 5 s.
// Bus is 100 kHz because the MLX90614 is an SMBus part. Grove VCC is 3.3 V.
// FQBN: Seeeduino:samd:seeed_wio_terminal

#include <Wire.h>

uint32_t nmeaCount = 0, byteCount = 0;   // '$' chars / all bytes seen on Serial1 since the last report
char     peek[61]; uint8_t peekLen = 0;  // first printable chars of the period, to eyeball NMEA vs garbage
uint32_t gpsBaud = 115200, gpsProbeStart = 0, gpsSwitches = 0;
bool     gpsSawNmea = false;

void gpsSetBaud(uint32_t b) { Serial1.begin(b); gpsBaud = b; gpsProbeStart = millis(); gpsSawNmea = false; }

// Called every loop(): if nothing valid arrives for 2.5 s, try the other baud. When NMEA shows
// up at 9600, send $PCAS01,5 (module -> 115200) and re-listen at 115200; count the attempts.
void gpsAutoBaud() {
  if (gpsSawNmea && gpsBaud == 9600) {
    Serial1.println("$PCAS01,5*19");
    Serial1.flush();
    delay(100);
    gpsSwitches++;
    Serial.printf("GPS: NMEA seen at 9600 -> sent $PCAS01,5 (attempt %lu), listening at 115200\n", (unsigned long)gpsSwitches);
    gpsSetBaud(115200);
    return;
  }
  if (!gpsSawNmea && millis() - gpsProbeStart > 2500) {
    gpsSetBaud(gpsBaud == 115200 ? 9600 : 115200);
    Serial.printf("GPS: nothing valid, now listening at %lu\n", (unsigned long)gpsBaud);
  }
}

const char* nameFor(uint8_t a) {
  switch (a) {
    case 0x29: return "TSL2591 optical";
    case 0x30: return "MMC5603 magnetometer";
    case 0x55: return "BQ27441 fuel gauge (chassis)";
    case 0x5A: return "MLX90614 thermal IR";
    case 0x76: case 0x77: return "BME280 env";
    default:   return "unknown";
  }
}

void i2cScan() {
  Serial.println("--- I2C scan (Wire, 100 kHz) ---");
  int found = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  0x%02X  %s\n", addr, nameFor(addr));
      found++;
    }
  }
  Serial.printf("--- %d device(s) ---\n", found);
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Wire.begin();
  Wire.setClock(100000);   // MLX90614 is SMBus: 100 kHz max for the whole bus

  // Air530Z on the chassis UART socket = Serial1. Auto-baud (see gpsSetBaud): the module
  // boots at 9600 unless it persisted 115200, and the SAMD core DROPS bytes with framing
  // errors, so listening at the wrong baud looks like "0 bytes", not garbage.
  gpsSetBaud(115200);
}

void loop() {
  static uint32_t lastReport = 0;
  while (Serial1.available()) {
    char c = Serial1.read(); byteCount++;
    if (c == '$') { nmeaCount++; gpsSawNmea = true; }
    if (peekLen < 60 && c >= 32 && c < 127) peek[peekLen++] = c;
  }
  gpsAutoBaud();
  if (millis() - lastReport >= 5000) {
    lastReport = millis();
    i2cScan();
    peek[peekLen] = 0;
    Serial.printf("GPS on Serial1 @%lu: %lu bytes, %lu '$' in 5 s", (unsigned long)gpsBaud, (unsigned long)byteCount, (unsigned long)nmeaCount);
    if (byteCount == 0)      Serial.print("  <-- nothing: UART socket / cable / GPS power");
    else if (nmeaCount == 0) Serial.print("  <-- bytes but no '$': baud mismatch");
    Serial.println();
    if (peekLen) { Serial.print("  first chars: "); Serial.println(peek); }
    nmeaCount = byteCount = 0; peekLen = 0;
  }
}
