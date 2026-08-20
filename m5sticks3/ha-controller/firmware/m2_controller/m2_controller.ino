// Home Assistant controller for M5StickS3 — WebSocket, live state, on/off control.
// Config from .env via secrets.h + generated_entities.h (run tools/gen_config.py).
//
// FQBN: esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB

#include <M5Unified.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include "secrets.h"
#include "config.h"

using namespace websockets;

WebsocketsClient ws;
bool authed = false;
int  nextId = 3;

struct CacheEntry { char state[24]; bool valid; };
CacheEntry cache[ENTITY_COUNT];

enum Page { PAGE_CONTROLS, PAGE_DASHBOARD };
Page page = PAGE_CONTROLS;
int  highlight = 0;                 // index into ENTITIES of the highlighted CONTROL
bool keyALongFired = false;

int firstControl() {
  for (int i = 0; i < ENTITY_COUNT; i++) if (ENTITIES[i].role == ENT_CONTROL) return i;
  return 0;
}
int nextControl(int from) {
  for (int step = 1; step <= ENTITY_COUNT; step++) {
    int i = (from + step) % ENTITY_COUNT;
    if (ENTITIES[i].role == ENT_CONTROL) return i;
  }
  return from;
}

// Returns true only if eid is a configured entity (so we redraw just for those,
// not for the flood of house-wide state_changed events we don't display).
bool applyState(const char* eid, const char* st) {
  for (int i = 0; i < ENTITY_COUNT; i++) {
    if (!strcmp(ENTITIES[i].entity_id, eid)) {
      strncpy(cache[i].state, st, sizeof(cache[i].state) - 1);
      cache[i].state[sizeof(cache[i].state) - 1] = 0;
      cache[i].valid = true;
      return true;
    }
  }
  return false;
}

void drawAll();  // fwd decl

// ---------- WebSocket ----------

void sendJson(const String& s) { ws.send(s); }

void onMessage(WebsocketsMessage m) {
  JsonDocument filter;
  filter["type"] = true;
  filter["event"]["data"]["entity_id"] = true;
  filter["event"]["data"]["new_state"]["state"] = true;
  filter["result"][0]["entity_id"] = true;
  filter["result"][0]["state"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, m.data(), DeserializationOption::Filter(filter))) return;

  const char* type = doc["type"];
  if (!type) return;

  if (!strcmp(type, "auth_required")) {
    sendJson(String("{\"type\":\"auth\",\"access_token\":\"") + HA_TOKEN + "\"}");
  } else if (!strcmp(type, "auth_ok")) {
    authed = true;
    sendJson("{\"id\":1,\"type\":\"get_states\"}");
    sendJson("{\"id\":2,\"type\":\"subscribe_events\",\"event_type\":\"state_changed\"}");
    nextId = 3;
    drawAll();
  } else if (!strcmp(type, "result")) {
    JsonArray arr = doc["result"].as<JsonArray>();
    if (!arr.isNull()) {                     // get_states snapshot
      for (JsonObject o : arr) {
        const char* eid = o["entity_id"];
        const char* st  = o["state"];
        if (eid && st) applyState(eid, st);
      }
      drawAll();
    }
  } else if (!strcmp(type, "event")) {
    const char* eid = doc["event"]["data"]["entity_id"];
    const char* st  = doc["event"]["data"]["new_state"]["state"];
    if (eid && st && applyState(eid, st)) drawAll();  // redraw only for our entities
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
  authed = false;
  ws.connect(String("ws://") + HA_HOST + ":" + HA_PORT + "/api/websocket");
}

// ---------- display ----------

void drawHeader() {
  M5.Display.fillRect(0, 0, 240, 18, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.drawString(page == PAGE_CONTROLS ? "Controls" : "Dashboard", 0, 0);
  uint16_t c = authed ? TFT_GREEN
             : (WiFi.status() == WL_CONNECTED ? TFT_YELLOW : TFT_RED);
  M5.Display.fillCircle(150, 8, 5, c);
  int bat = M5.Power.getBatteryLevel();
  char b[8]; snprintf(b, sizeof(b), "%3d%%", bat >= 0 ? bat : 0);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.drawString(b, 210, 4);
}

void drawControls() {
  M5.Display.fillRect(0, 20, 240, 115, TFT_BLACK);
  M5.Display.setTextSize(2);
  int y = 24;
  for (int i = 0; i < ENTITY_COUNT; i++) {
    if (ENTITIES[i].role != ENT_CONTROL) continue;
    bool on = cache[i].valid && !strcmp(cache[i].state, "on");
    bool sel = (i == highlight);
    if (sel) M5.Display.fillRect(0, y - 2, 240, 20, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, sel ? TFT_NAVY : TFT_BLACK);
    M5.Display.drawString(ENTITIES[i].label, 6, y);
    const char* s = !cache[i].valid ? "..." : (on ? "ON" : "OFF");
    M5.Display.setTextColor(!cache[i].valid ? TFT_DARKGREY : (on ? TFT_GREEN : TFT_DARKGREY),
                            sel ? TFT_NAVY : TFT_BLACK);
    M5.Display.drawString(s, 180, y);
    y += 24;
  }
  if (!authed) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.drawString("offline - controls disabled", 6, 118);
  }
}

void drawAll() {
  drawHeader();
  if (page == PAGE_CONTROLS) drawControls();
  // dashboard drawn in Task 5
}

// ---------- controls ----------

void pollButtons() {
  if (M5.BtnA.wasClicked()) {            // KEY1: move highlight
    highlight = nextControl(highlight);
    drawControls();
  }
  if (M5.BtnA.pressedFor(3000)) {        // KEY1 long: switch page
    if (!keyALongFired) {
      keyALongFired = true;
      page = (page == PAGE_CONTROLS) ? PAGE_DASHBOARD : PAGE_CONTROLS;
      M5.Speaker.tone(1600, 100);
      M5.Display.fillScreen(TFT_BLACK);
      drawAll();
    }
  } else if (M5.BtnA.wasReleased()) {
    keyALongFired = false;
  }
  if (M5.BtnB.wasClicked()) {            // KEY2: toggle highlighted control
    if (authed && ENTITIES[highlight].role == ENT_CONTROL) sendToggle(ENTITIES[highlight].entity_id);
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  highlight = firstControl();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  drawHeader();
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(200);

  ws.onMessage(onMessage);
  connectWs();
  drawAll();
}

uint32_t lastTry = 0;
void loop() {
  M5.update();
  pollButtons();
  ws.poll();
  if (WiFi.status() == WL_CONNECTED && !ws.available() && millis() - lastTry > 3000) {
    lastTry = millis();
    connectWs();
    drawHeader();
  }
}
