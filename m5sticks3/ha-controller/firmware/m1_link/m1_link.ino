// Milestone 1 — HA link bring-up: WiFi + WebSocket auth + state subscribe.
// Serial only. Proves the whole Home Assistant path (auth, live state_changed,
// and a control toggle) before any UI. Config comes from .env via secrets.h
// (run tools/gen_config.py first).
//
// FQBN: esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB
// Serial command: type "TOGGLE" to toggle HA_TEST_ENTITY.

#include <M5Unified.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include "secrets.h"

using namespace websockets;

WebsocketsClient ws;
bool authed = false;
int  nextId = 3;

void sendJson(const String& s) { ws.send(s); Serial.println("TX " + s); }

void onMessage(WebsocketsMessage m) {
  JsonDocument filter;               // keep only the fields we use
  filter["type"] = true;
  filter["event"]["event_type"] = true;
  filter["event"]["data"]["entity_id"] = true;
  filter["event"]["data"]["new_state"]["state"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, m.data(), DeserializationOption::Filter(filter))) return;

  const char* type = doc["type"];
  if (!type) return;

  if (!strcmp(type, "auth_required")) {
    sendJson(String("{\"type\":\"auth\",\"access_token\":\"") + HA_TOKEN + "\"}");
  } else if (!strcmp(type, "auth_ok")) {
    authed = true;
    Serial.println("AUTH_OK");
    // m1 validates live changes only (no entity list here to scope a snapshot).
    sendJson("{\"id\":2,\"type\":\"subscribe_events\",\"event_type\":\"state_changed\"}");
    nextId = 3;
  } else if (!strcmp(type, "auth_invalid")) {
    Serial.println("AUTH_INVALID (check HA_TOKEN in .env)");
  } else if (!strcmp(type, "event")) {
    const char* et = doc["event"]["event_type"];
    if (et && !strcmp(et, "state_changed")) {
      const char* eid = doc["event"]["data"]["entity_id"];
      const char* st  = doc["event"]["data"]["new_state"]["state"];
      if (eid && st) Serial.printf("STATE %s = %s\n", eid, st);
    }
  }
}

void sendToggle(const char* entity) {
  JsonDocument d;
  d["id"] = nextId++;
  d["type"] = "call_service";
  d["domain"] = "homeassistant";
  d["service"] = "toggle";
  d["target"]["entity_id"] = entity;
  String out; serializeJson(d, out);
  sendJson(out);
}

void connectWs() {
  String url = String("ws://") + HA_HOST + ":" + HA_PORT + "/api/websocket";
  Serial.println("WS connect " + url);
  authed = false;
  ws.connect(url);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(1);
  M5.Display.setTextSize(2);
  M5.Display.drawString("m1 link", 4, 4);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.println(" connected: " + WiFi.localIP().toString());

  ws.onMessage(onMessage);
  ws.onEvent([](WebsocketsEvent e, String){
    if (e == WebsocketsEvent::ConnectionClosed) Serial.println("WS closed");
  });
  connectWs();
}

uint32_t lastPoll = 0;
void loop() {
  ws.poll();

  if (WiFi.status() == WL_CONNECTED && !ws.available() && millis() - lastPoll > 2000) {
    lastPoll = millis();
    connectWs();   // simple reconnect
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n'); cmd.trim();
    if (cmd == "TOGGLE") {
      if (authed) sendToggle(HA_TEST_ENTITY);
      else Serial.println("not authed yet");
    }
  }
}
