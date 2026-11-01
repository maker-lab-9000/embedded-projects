// Bring-up (Task 1): WiFi, NTP, one HA websocket auth, subscribe, history.
// Prints sizes over serial. Exactly one auth attempt per boot.
#include <rpcWiFi.h>
#include <WiFiUdp.h>
#include "secrets.h"
#include "ws_client.h"

#include <unistd.h>
static int freeRam() { char top; return &top - reinterpret_cast<char*>(sbrk(0)); }

static WsClient ws;

static uint32_t ntpOnce() {
  WiFiUDP udp;
  udp.begin(2390);
  uint8_t pkt[48] = {0};
  pkt[0] = 0x1B;  // LI 0, version 3, client mode
  udp.beginPacket("pool.ntp.org", 123);
  udp.write(pkt, 48);
  udp.endPacket();
  uint32_t t0 = millis();
  while (millis() - t0 < 3000) {
    if (udp.parsePacket() >= 48) {
      udp.read(pkt, 48);
      udp.stop();
      uint32_t secs = (uint32_t)pkt[40] << 24 | (uint32_t)pkt[41] << 16 | (uint32_t)pkt[42] << 8 | pkt[43];
      return secs - 2208988800UL;
    }
    delay(10);
  }
  udp.stop();
  return 0;
}

static bool waitMsg(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (ws.poll()) return true;
    delay(5);
  }
  return false;
}

static void printHead(const char* label) {
  char head[61];
  strncpy(head, ws.msg(), 60); head[60] = 0;
  Serial.print(label); Serial.print(" len="); Serial.print(ws.msgLen());
  Serial.print(" : "); Serial.println(head);
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 4000) {}
  Serial.println("\n== m1_tram bring-up ==");
  Serial.print("free RAM: "); Serial.println(freeRam());
  Serial.print("RTL8720 fw: "); Serial.println(rpc_system_version());

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);
  if (WiFi.status() != WL_CONNECTED) { Serial.println("WiFi: FAILED"); return; }
  Serial.print("WiFi IP: "); Serial.println(WiFi.localIP());
  Serial.print("RSSI: "); Serial.println(WiFi.RSSI());

  uint32_t now = ntpOnce();
  Serial.print("NTP epoch: "); Serial.println(now);

  t0 = millis();
  if (!ws.connect(HA_HOST, HA_PORT, "/api/websocket")) { Serial.println("WS connect: FAILED"); return; }
  Serial.print("WS connected in ms: "); Serial.println(millis() - t0);
  if (!waitMsg(5000)) { Serial.println("no auth_required"); return; }
  printHead("rx");
  ws.sendText("{\"type\":\"auth\",\"access_token\":\"" HA_TOKEN "\"}");
  if (!waitMsg(5000)) { Serial.println("no auth reply"); return; }
  printHead("rx");
  if (!strstr(ws.msg(), "auth_ok")) { Serial.println("AUTH NOT OK - stopping, no retry"); ws.close(); return; }

  ws.sendText("{\"id\":1,\"type\":\"subscribe_entities\",\"entity_ids\":["
              "\"sensor.kuckhoffstr_berlin\","
              "\"input_number.home_m1_tram_minutes_between_trams\","
              "\"sensor.home_m1_tram_last_departed\"]}");
  for (int i = 0; i < 2 && waitMsg(5000); i++) printHead("sub");

  if (now) {
    char req[300];
    uint32_t s = now - 86400;
    // ISO date from epoch (UTC), good enough for the bring-up.
    uint32_t days = s / 86400, rem = s % 86400;
    int y = 1970; while (true) { int dy = (y % 4 == 0 && (y % 100 || y % 400 == 0)) ? 366 : 365; if (days < (uint32_t)dy) break; days -= dy; y++; }
    static const uint8_t md[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int m = 0; while (true) { int d = md[m] + (m == 1 && (y % 4 == 0 && (y % 100 || y % 400 == 0))); if (days < (uint32_t)d) break; days -= d; m++; }
    snprintf(req, sizeof(req),
             "{\"id\":2,\"type\":\"history/history_during_period\",\"start_time\":\"%04d-%02d-%02dT%02lu:%02lu:00Z\","
             "\"entity_ids\":[\"input_number.home_m1_tram_minutes_between_trams\"],"
             "\"minimal_response\":true,\"no_attributes\":true}",
             y, m + 1, (int)days + 1, (unsigned long)(rem / 3600), (unsigned long)(rem % 3600 / 60));
    ws.sendText(req);
    if (waitMsg(8000)) printHead("hist");
    else Serial.println("no history reply");
  }
  Serial.print("dropped msgs: "); Serial.println(ws.dropped());
  Serial.print("free RAM: "); Serial.println(freeRam());
  Serial.println("== listening for changes ==");
}

void loop() {
  if (ws.poll()) printHead("chg");
}
