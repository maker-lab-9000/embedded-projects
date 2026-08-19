// WiFi presence scanner (device radar) for M5StickS3 — receive-only.
// Counts nearby active WiFi devices via promiscuous-mode sniffing.
//
// FQBN: esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB
// Port: /dev/cu.usbmodem* (opening the port resets the device)

#include <M5Unified.h>
#include <WiFi.h>
#include "esp_wifi.h"

const int      CHANNEL_MIN = 1, CHANNEL_MAX = 13;
const uint32_t DWELL_MS    = 250;
const uint32_t WINDOW_MS   = 60000;
const int      TABLE_LEN   = 128;
const int      NEAR_DBM    = -55;
const int      NEARBY_DBM  = -75;
const float    RSSI_ALPHA  = 0.3f;

const uint32_t SPARK_PERIOD_MS = 15000;   // 236 samples * 15 s ~= 59 min
const int SPARK_X = 2, SPARK_Y = 64, SPARK_W = 236, SPARK_H = 44;
const int SPARK_N = SPARK_W;  // one sample per column max

int spark[SPARK_N];
int sparkLen = 0;
uint32_t lastSparkMs = 0;

struct Device { uint8_t mac[6]; uint32_t lastSeenMs; float rssiEma; uint8_t kind; };
Device table[TABLE_LEN];
int tableCount = 0;
portMUX_TYPE tableMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t framesSinceTick = 0;

int findMac(const uint8_t* mac) {
  for (int i = 0; i < tableCount; i++)
    if (memcmp(table[i].mac, mac, 6) == 0) return i;
  return -1;
}

// Called under tableMux.
void tableUpsert(const uint8_t* mac, int8_t rssi, uint8_t kind) {
  uint32_t now = millis();
  int i = findMac(mac);
  if (i < 0) {
    if (tableCount < TABLE_LEN) {
      i = tableCount++;
    } else {  // evict oldest
      i = 0;
      for (int j = 1; j < tableCount; j++)
        if (table[j].lastSeenMs < table[i].lastSeenMs) i = j;
    }
    memcpy(table[i].mac, mac, 6);
    table[i].rssiEma = rssi;
    table[i].kind = kind;
  } else {
    table[i].rssiEma = RSSI_ALPHA * rssi + (1 - RSSI_ALPHA) * table[i].rssiEma;
    if (kind == 3) table[i].kind = 3;  // data-frame client is the strongest signal
  }
  table[i].lastSeenMs = now;
}

void onRx(void* buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t* p = (wifi_promiscuous_pkt_t*)buf;
  int8_t rssi = p->rx_ctrl.rssi;
  const uint8_t* d = p->payload;
  uint8_t fc0 = d[0];
  uint8_t ftype = (fc0 >> 2) & 0x3;
  uint8_t fsubtype = (fc0 >> 4) & 0xF;
  const uint8_t* src = d + 10;  // Address 2 (transmitter) for mgmt/data frames

  uint8_t kind = 0;
  if (ftype == 0) {
    if (fsubtype == 8 || fsubtype == 5) kind = 1;       // AP
    else if (fsubtype == 4) kind = 2;                    // prober
  } else if (ftype == 2) {
    kind = 3;                                            // client
  }
  if (kind == 0) return;
  if (src[0] & 0x01) return;  // skip group/broadcast source

  framesSinceTick++;
  portENTER_CRITICAL(&tableMux);
  tableUpsert(src, rssi, kind);
  portEXIT_CRITICAL(&tableMux);
}

void expireOld() {  // called under tableMux
  uint32_t now = millis();
  int w = 0;
  for (int i = 0; i < tableCount; i++) {
    if (now - table[i].lastSeenMs <= WINDOW_MS) {
      if (w != i) table[w] = table[i];
      w++;
    }
  }
  tableCount = w;
}

int countNearby() {
  int n = 0;
  portENTER_CRITICAL(&tableMux);
  for (int i = 0; i < tableCount; i++)
    if (table[i].kind != 1 && table[i].rssiEma >= NEARBY_DBM) n++;
  portEXIT_CRITICAL(&tableMux);
  return n;
}
int countTotalDevices() {
  int n = 0;
  portENTER_CRITICAL(&tableMux);
  for (int i = 0; i < tableCount; i++) if (table[i].kind != 1) n++;
  portEXIT_CRITICAL(&tableMux);
  return n;
}
int countAPs() {
  int n = 0;
  portENTER_CRITICAL(&tableMux);
  for (int i = 0; i < tableCount; i++) if (table[i].kind == 1) n++;
  portEXIT_CRITICAL(&tableMux);
  return n;
}

uint32_t lastTickMs = 0, lastHopMs = 0;
int channel = CHANNEL_MIN;

// ---------- display ----------

uint16_t countColor(int n) {
  if (n == 0) return TFT_DARKGREY;
  if (n <= 3) return TFT_GREEN;
  if (n <= 9) return TFT_YELLOW;
  return TFT_RED;
}

void drawCounts() {
  char line[24];
  int nearby = countNearby();

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.drawString("WiFi Radar", 0, 0);
  snprintf(line, sizeof(line), "ch %2d ", channel);
  M5.Display.drawString(line, 160, 0);

  snprintf(line, sizeof(line), "%3d", nearby);   // number only (size 4 = ~72 px)
  M5.Display.setTextSize(4);
  M5.Display.setTextColor(countColor(nearby), TFT_BLACK);
  M5.Display.drawString(line, 0, 20);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.drawString("near", 4, 54);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  snprintf(line, sizeof(line), "%3d devs ", countTotalDevices());
  M5.Display.drawString(line, 104, 24);
  snprintf(line, sizeof(line), "%3d APs ", countAPs());
  M5.Display.drawString(line, 104, 44);
}

void drawSparkline() {
  M5.Display.fillRect(SPARK_X, SPARK_Y, SPARK_W, SPARK_H, TFT_BLACK);
  M5.Display.drawRect(SPARK_X, SPARK_Y, SPARK_W, SPARK_H, TFT_DARKGREY);
  if (sparkLen < 2) return;
  int mx = 5;
  for (int i = 0; i < sparkLen; i++) if (spark[i] > mx) mx = spark[i];
  int prevX = -1, prevY = 0;
  for (int i = 0; i < sparkLen; i++) {
    int x = SPARK_X + 2 + (int)((long)(SPARK_W - 4) * i / (SPARK_N - 1));
    int y = SPARK_Y + SPARK_H - 2 - (int)((SPARK_H - 4) * spark[i] / mx);
    if (prevX >= 0) M5.Display.drawLine(prevX, prevY, x, y, TFT_CYAN);
    prevX = x; prevY = y;
  }
}

void drawDiag(uint32_t fps) {
  char diag[48];
  uint32_t upMin = millis() / 60000;
  int bat = M5.Power.getBatteryLevel();
  if (bat >= 0)
    snprintf(diag, sizeof(diag), "fps %3lu  bat %3d%%  up %lu:%02lu  ",
             (unsigned long)fps, bat, (unsigned long)(upMin/60), (unsigned long)(upMin%60));
  else
    snprintf(diag, sizeof(diag), "fps %3lu  bat --  up %lu:%02lu  ",
             (unsigned long)fps, (unsigned long)(upMin/60), (unsigned long)(upMin%60));
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.drawString(diag, 4, 124);
}

void pushSpark(int v) {
  if (sparkLen < SPARK_N) {
    spark[sparkLen++] = v;
  } else {
    for (int i = 1; i < SPARK_N; i++) spark[i-1] = spark[i];
    spark[SPARK_N-1] = v;
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);

  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&onRx);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  drawSparkline();  // empty box immediately
}

void loop() {
  M5.update();

  if (millis() - lastHopMs >= DWELL_MS) {
    lastHopMs = millis();
    channel++;
    if (channel > CHANNEL_MAX) channel = CHANNEL_MIN;
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  }

  if (millis() - lastTickMs >= 1000) {
    lastTickMs = millis();
    portENTER_CRITICAL(&tableMux);
    expireOld();
    portEXIT_CRITICAL(&tableMux);
    uint32_t fps = framesSinceTick;
    framesSinceTick = 0;
    drawCounts();
    drawDiag(fps);
    if (millis() - lastSparkMs >= SPARK_PERIOD_MS) {
      lastSparkMs = millis();
      pushSpark(countNearby());
      drawSparkline();
    }
    Serial.printf("nearby=%d devs=%d aps=%d fps=%lu ch=%d\n",
                  countNearby(), countTotalDevices(), countAPs(),
                  (unsigned long)fps, channel);
  }
}
