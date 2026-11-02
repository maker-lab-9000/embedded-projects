#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_STR(a, b) do { if (strcmp((a), (b)) == 0) g_pass++; else { g_fail++; printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

#include "ws_frame.h"

static void test_ws_encode_small() {
  uint8_t out[14]; const uint8_t mask[4] = {1, 2, 3, 4};
  size_t n = wsEncodeHeader(out, WS_OP_TEXT, 5, mask);
  CHECK(n == 6);
  CHECK(out[0] == 0x81);           // FIN + text
  CHECK(out[1] == (0x80 | 5));     // masked, len 5
  CHECK(out[2] == 1 && out[5] == 4);
}

static void test_ws_encode_medium() {
  uint8_t out[14]; const uint8_t mask[4] = {0, 0, 0, 0};
  size_t n = wsEncodeHeader(out, WS_OP_TEXT, 300, mask);
  CHECK(n == 8);
  CHECK(out[1] == (0x80 | 126));
  CHECK(out[2] == 0x01 && out[3] == 0x2C);  // 300
}

static void test_ws_parse_server_frames() {
  WsHeader h;
  const uint8_t small[] = {0x81, 0x03, 'a', 'b', 'c'};
  CHECK(wsParseHeader(small, 1, h) == 0);   // need more
  CHECK(wsParseHeader(small, 5, h) == 2);
  CHECK(h.fin && h.opcode == WS_OP_TEXT && !h.masked && h.len == 3);

  const uint8_t med[] = {0x01, 126, 0x10, 0x00};  // not FIN, text, len 4096
  CHECK(wsParseHeader(med, 3, h) == 0);
  CHECK(wsParseHeader(med, 4, h) == 4);
  CHECK(!h.fin && h.len == 4096);

  const uint8_t big[] = {0x80, 127, 0, 0, 0, 0, 0, 1, 0, 0};  // continuation, len 65536
  CHECK(wsParseHeader(big, 10, h) == 10);
  CHECK(h.opcode == WS_OP_CONT && h.len == 65536);
}

static void test_ws_mask_roundtrip() {
  uint8_t d[] = {'h', 'e', 'l', 'l', 'o'}; const uint8_t m[4] = {0x37, 0xfa, 0x21, 0x3d};
  wsMask(d, 5, m, 0);
  CHECK(d[0] == ('h' ^ 0x37) && d[4] == ('o' ^ 0x37));
  wsMask(d, 2, m, 0); wsMask(d + 2, 3, m, 2);  // split at offset 2 must equal one pass
  CHECK(memcmp(d, "hello", 5) == 0);
}


#include "timeutil.h"

static void test_parse_iso() {
  uint32_t e = 0;
  CHECK(parseIso8601("2026-11-01T22:58:00+01:00", e) && e == 1793570280u);
  CHECK(parseIso8601("2026-11-01T21:35:00+00:00", e) && e == 1793568900u);
  CHECK(parseIso8601("2026-11-01T21:35:00Z", e) && e == 1793568900u);
  CHECK(parseIso8601("2026-11-01T21:48:56.370205+00:00", e) && e == 1793569736u);
  CHECK(parseIso8601("2026-11-01T23:35:00+02:00", e) && e == 1793568900u);
  CHECK(!parseIso8601("N/A", e));
  CHECK(!parseIso8601("", e));
  CHECK(!parseIso8601("unavailable", e));
}

static void test_berlin_offset() {
  CHECK(berlinOffset(1774745940u) == 3600);  // 2026-03-29 00:59Z
  CHECK(berlinOffset(1774746000u) == 7200);  // 01:00Z, summer time starts
  CHECK(berlinOffset(1792889940u) == 7200);  // 2026-10-25 00:59Z
  CHECK(berlinOffset(1792890000u) == 3600);  // 01:00Z, winter time
}

static void test_format_clock() {
  char b[8];
  formatClock(1793570280u, b, sizeof(b));
  CHECK_STR(b, "22:58");
  formatClock(1774746000u, b, sizeof(b));  // 01:00Z in summer time
  CHECK_STR(b, "03:00");
}

static void test_countdown_clamps() {
  char b[16]; const uint32_t now = 1793570000u;
  formatCountdown(now, now - 30, b, sizeof(b)); CHECK_STR(b, "now");
  formatCountdown(now, now + 59, b, sizeof(b)); CHECK_STR(b, "now");
  formatCountdown(now, now + 245, b, sizeof(b)); CHECK_STR(b, "in 4 min");
}

static void test_unsynced_time() {
  char b[16];
  formatCountdown(0, 1793570000u, b, sizeof(b)); CHECK_STR(b, "--");
  formatAgo(0, 1793570000u, b, sizeof(b)); CHECK_STR(b, "--");
  formatAgo(1793570000u, 0, b, sizeof(b)); CHECK_STR(b, "--");
}

static void test_format_ago() {
  char b[16]; const uint32_t now = 1793570000u;
  formatAgo(now, now - 12, b, sizeof(b)); CHECK_STR(b, "12 sec ago");
  formatAgo(now, now - 180, b, sizeof(b)); CHECK_STR(b, "3 min ago");
  formatAgo(now, now - 7300, b, sizeof(b)); CHECK_STR(b, "2 h ago");
  formatAgo(now, now + 5, b, sizeof(b)); CHECK_STR(b, "0 sec ago");  // clock skew
}

#include "tram_model.h"

static const char INITIAL[] = R"({"a":{
 "sensor.kuckhoffstr_berlin":{"s":"Next M1 at 22:58","a":{"departures":[
   {"line_name":"M1","line_type":"tram","time":"22:58","timestamp":"2026-11-01T22:58:00+01:00","direction":"Schönhauser Allee/Bornholmer Str. -> Bus -> extra long tail","color":"#D82020","delay":120,"cancelled":false},
   {"line_name":"M1","line_type":"tram","time":"23:08","timestamp":"2026-11-01T23:08:00+01:00","direction":"Niederschönhausen, Schillerstr.","color":"#D82020","delay":0,"cancelled":true},
   {"line_name":"M1","line_type":"tram","time":"23:18","timestamp":"2026-11-01T23:18:00+01:00","direction":"Mitte, Am Kupfergraben","color":"#D82020","delay":null,"cancelled":false},
   {"line_name":"M1","line_type":"tram","time":"23:38","timestamp":"2026-11-01T23:38:00+01:00","direction":"Niederschönhausen, Schillerstr.","color":"#D82020","delay":0,"cancelled":false}
 ],"icon":"mdi:clock","friendly_name":"Kuckhoffstr. (Berlin) Departures"},"c":"x","lc":1793569000.5,"lu":1793569000.5},
 "input_number.home_m1_tram_minutes_between_trams":{"s":"20.0","a":{"unit_of_measurement":"min"},"c":"y","lc":1793569736.37},
 "sensor.home_m1_tram_last_departed":{"s":"2026-11-01T21:35:00+00:00","a":{"device_class":"timestamp"},"c":"z","lc":1793569737.2}
}})";

static void applyJson(TramState& s, const char* json) {
  JsonDocument d;
  CHECK(deserializeJson(d, json) == DeserializationError::Ok);
  tramApplyEvent(s, d.as<JsonVariantConst>());
}

static void test_initial_event() {
  TramState s{};
  applyJson(s, INITIAL);
  CHECK(s.depKnown && s.depCount == 3);
  CHECK_STR(s.dep[0].line, "M1");
  CHECK_STR(s.dep[0].time, "22:58");
  CHECK(s.dep[0].epoch == 1793570280u);
  CHECK(s.dep[0].delayMin == 2);
  CHECK(s.dep[0].color565 == 0xD904);
  CHECK(strlen(s.dep[0].dir) == 39);
  CHECK(strncmp(s.dep[0].dir, "Schoenhauser Allee", 18) == 0);  // umlaut folded
  CHECK_STR(s.dep[1].time, "23:18");                            // cancelled one skipped
  CHECK(s.dep[1].delayMin == 0);                                // null delay
  CHECK_STR(s.dep[2].time, "23:38");
  CHECK(s.gapKnown && s.gapMin == 20.0f);
  CHECK(s.lastDeparted == 1793568900u);
  CHECK(s.dirty == (DIRTY_NEXT | DIRTY_GAP | DIRTY_LAST));
}

static void test_gap_change_only() {
  TramState s{};
  applyJson(s, INITIAL);
  s.dirty = 0;
  applyJson(s, R"({"c":{"input_number.home_m1_tram_minutes_between_trams":{"+":{"s":"21.0","lc":1793570000.1}}}})");
  CHECK(s.gapMin == 21.0f);
  CHECK(s.dirty == DIRTY_GAP);
  CHECK(s.depCount == 3);
}

static void test_departures_change() {
  TramState s{};
  applyJson(s, INITIAL);
  s.dirty = 0;
  applyJson(s, R"({"c":{"sensor.kuckhoffstr_berlin":{"+":{"s":"Next M1 at 23:18","a":{"departures":[
    {"line_name":"M1","time":"23:18","timestamp":"2026-11-01T23:18:00+01:00","direction":"Mitte","color":"#D82020","delay":0,"cancelled":false}]}}}}})");
  CHECK(s.depCount == 1 && s.dirty == DIRTY_NEXT);
  CHECK_STR(s.dep[0].time, "23:18");
}

static void test_empty_and_cancelled() {
  TramState s{};
  applyJson(s, R"({"a":{"sensor.kuckhoffstr_berlin":{"s":"N/A","a":{"departures":[]}}}})");
  CHECK(s.depKnown && s.depCount == 0);
  applyJson(s, INITIAL);
  applyJson(s, R"({"c":{"sensor.kuckhoffstr_berlin":{"+":{"a":{"departures":[
    {"line_name":"M1","time":"23:18","timestamp":"2026-11-01T23:18:00+01:00","direction":"Mitte","color":"#D82020","delay":0,"cancelled":true}]}}}}})");
  CHECK(s.depCount == 0);
}

static void test_state_only_change_keeps_departures() {
  TramState s{};
  applyJson(s, INITIAL);
  s.dirty = 0;
  applyJson(s, R"({"c":{"sensor.kuckhoffstr_berlin":{"+":{"s":"Next M1 at 22:58","lu":1793570100.0}}}})");
  CHECK(s.depCount == 3 && s.dirty == 0);
}

static void test_gap_unavailable_and_unknown_entity() {
  TramState s{};
  applyJson(s, INITIAL);
  applyJson(s, R"({"c":{"input_number.home_m1_tram_minutes_between_trams":{"+":{"s":"unavailable"}},"light.other":{"+":{"s":"on"}}}})");
  CHECK(!s.gapKnown);
  applyJson(s, R"({"c":{"sensor.home_m1_tram_last_departed":{"+":{"s":"unknown"}}}})");
  CHECK(s.lastDeparted == 0);
}

static void applyHist(TramState& s, const char* json) {
  JsonDocument d;
  CHECK(deserializeJson(d, json) == DeserializationError::Ok);
  tramApplyHistory(s, d.as<JsonVariantConst>());
}

static void test_history_skips_non_numeric() {
  TramState s{};
  applyHist(s, R"({"input_number.home_m1_tram_minutes_between_trams":[
    {"s":"20.0","a":{},"lu":1793500000.1},{"s":"unavailable","lu":1793500100.0},{"s":"30","lc":1793500200.0}]})");
  CHECK(s.histKnown && s.histCount == 2);
  CHECK(s.hist[0].t == 1793500000u && s.hist[0].v == 20.0f);
  CHECK(s.hist[1].t == 1793500200u && s.hist[1].v == 30.0f);
  CHECK(s.dirty & DIRTY_CHART);
  applyHist(s, "{}");
  CHECK(s.histKnown && s.histCount == 0);
}

static void test_history_keeps_latest_96() {
  std::string j = "{\"input_number.home_m1_tram_minutes_between_trams\":[";
  for (int i = 0; i < 120; i++) {
    if (i) j += ",";
    j += "{\"s\":\"" + std::to_string(i) + "\",\"lu\":" + std::to_string(1793400000 + i * 60) + "}";
  }
  j += "]}";
  TramState s{};
  applyHist(s, j.c_str());
  CHECK(s.histCount == 96);
  CHECK(s.hist[95].v == 119.0f && s.hist[0].v == 24.0f);
}

static void test_hex_to_565() {
  CHECK(hexTo565("#D82020") == 0xD904);
  CHECK(hexTo565("#FFFFFF") == 0xFFFF);
  CHECK(hexTo565("bogus") == 0xF800);
  CHECK(hexTo565(nullptr) == 0xF800);
}

#include "ha_proto.h"

static ClimateState g_climate{};  // tram tests don't look at these
static LightsState g_lights{};
static HaAction handle(HaProto& p, TramState& s, const char* json) {
  JsonDocument d;
  CHECK(deserializeJson(d, json) == DeserializationError::Ok);
  return haHandleMessage(p, s, g_climate, g_lights, d.as<JsonVariantConst>());
}

static void test_auth_flow() {
  HaProto p; TramState s{};
  CHECK(handle(p, s, R"({"type":"auth_required","ha_version":"2026.10.4"})") == HA_ACT_SEND_AUTH);
  CHECK(!p.authed);
  CHECK(handle(p, s, R"({"type":"auth_ok","ha_version":"2026.10.4"})") == HA_ACT_SUBSCRIBE);
  CHECK(p.authed);
}

static void test_auth_invalid_rejects() {
  HaProto p; TramState s{};
  CHECK(handle(p, s, R"({"type":"auth_invalid","message":"Invalid access token or password"})") == HA_ACT_REJECTED);
  CHECK(!p.authed);
}

static void test_event_routing() {
  HaProto p; TramState s{};
  handle(p, s, R"({"type":"auth_ok"})");
  CHECK(handle(p, s, R"({"id":7,"type":"event","event":{"a":{"input_number.home_m1_tram_minutes_between_trams":{"s":"20.0"}}}})") == HA_ACT_NONE);
  CHECK(!s.gapKnown);  // not our subscription id
  CHECK(handle(p, s, R"({"id":1,"type":"event","event":{"a":{"input_number.home_m1_tram_minutes_between_trams":{"s":"20.0"}}}})") == HA_ACT_NONE);
  CHECK(s.gapKnown && s.gapMin == 20.0f);
}

static void test_history_result_routing() {
  HaProto p; TramState s{};
  p.histPending = 5;
  handle(p, s, R"({"id":4,"type":"result","success":true,"result":{"input_number.home_m1_tram_minutes_between_trams":[{"s":"20","lu":1793500000}]}})");
  CHECK(!s.histKnown);  // stale reply ignored
  handle(p, s, R"({"id":5,"type":"result","success":true,"result":{"input_number.home_m1_tram_minutes_between_trams":[{"s":"20","lu":1793500000}]}})");
  CHECK(s.histKnown && s.histCount == 1 && p.histPending == 0);
  p.histPending = 6;
  handle(p, s, R"({"id":6,"type":"result","success":false,"error":{"code":"x"}})");
  CHECK(p.histPending == 0 && s.histCount == 1);  // failure keeps old chart
}

static void test_pong_tracking() {
  HaProto p; TramState s{};
  p.pingPending = 9;
  handle(p, s, R"({"id":9,"type":"pong"})");
  CHECK(p.pingPending == 0);
}

static void test_backoff() {
  Backoff b;
  CHECK(b.next() == 5000u);
  CHECK(b.next() == 10000u);
  CHECK(b.next() == 20000u);
  for (int i = 0; i < 10; i++) b.next();
  CHECK(b.next() == 300000u);
  b.reset();
  CHECK(b.next() == 5000u);
}

static void test_format_iso_utc() {
  char b[24];
  formatIsoUtc(1793568900u, b, sizeof(b));
  CHECK_STR(b, "2026-11-01T21:35:00Z");
  formatIsoUtc(1774745940u, b, sizeof(b));
  CHECK_STR(b, "2026-03-29T00:59:00Z");
}

#include "wifi_pick.h"

static void test_pick_skips_unknown_security() {
  // Real scan 2026-11-01: same SSID on 5 GHz (unknown security, stronger) and 2.4 GHz WPA2.
  const BssInfo l[] = {{-47, 36, 6, true}, {-55, 11, 3, true}, {-60, 100, 3, false}};
  CHECK(pickBss(l, 3) == 1);
}

static void test_pick_strongest_supported() {
  const BssInfo l[] = {{-70, 1, 3, true}, {-50, 6, 4, true}, {-40, 11, 3, false}, {-45, 36, 2, true}};
  CHECK(pickBss(l, 4) == 3);
}

static void test_pick_none() {
  const BssInfo l[] = {{-47, 36, 6, true}, {-55, 11, 3, false}};
  CHECK(pickBss(l, 2) == -1);
  CHECK(pickBss(l, 0) == -1);
}

#include "chart.h"
#include "sleep_ctl.h"

static void test_chart_scale() {
  GapPoint a[] = {{1, 12.0f}, {2, 30.0f}, {3, 20.0f}};
  ChartScale c = chartScale(a, 3);
  CHECK(c.yMin == 10 && c.yMax == 32);
  GapPoint one[] = {{1, 20.0f}};
  c = chartScale(one, 1);
  CHECK(c.yMin == 18 && c.yMax == 22);
  c = chartScale(nullptr, 0);
  CHECK(c.yMin == 0 && c.yMax == 30);
  GapPoint low[] = {{1, 1.0f}};
  c = chartScale(low, 1);
  CHECK(c.yMin == 0 && c.yMax == 3);  // never below 0 minutes
}

static void test_chart_mapping() {
  const uint32_t now = 1793570000u;
  CHECK(chartX(now, now, 10, 200) == 210);
  CHECK(chartX(now - 86400, now, 10, 200) == 10);
  CHECK(chartX(now - 43200, now, 10, 200) == 110);
  CHECK(chartX(now - 999999, now, 10, 200) == 10);  // older than 24 h clamps left
  CHECK(chartX(now + 50, now, 10, 200) == 210);     // future clamps right
  ChartScale c{10, 30};
  CHECK(chartY(30, c, 100, 80) == 100);
  CHECK(chartY(10, c, 100, 80) == 180);
  CHECK(chartY(20, c, 100, 80) == 140);
}

static void test_sleep_button_c_toggles() {
  SleepCtl z;
  CHECK(sleepOnKey(z, KEY_C) == SLEEP_TURN_OFF && z.dark);
  CHECK(sleepOnKey(z, KEY_C) == SLEEP_TURN_ON && !z.dark);
}

static void test_sleep_other_keys_wake_only() {
  SleepCtl z;
  CHECK(sleepOnKey(z, KEY_A) == SLEEP_PASS);   // awake: key goes to the page
  sleepOnKey(z, KEY_C);
  CHECK(sleepOnKey(z, KEY_A) == SLEEP_TURN_ON && !z.dark);
  sleepOnKey(z, KEY_C);
  CHECK(sleepOnKey(z, KEY_JOY_LEFT) == SLEEP_TURN_ON);
  sleepOnKey(z, KEY_C);
  CHECK(sleepOnKey(z, KEY_B) == SLEEP_TURN_ON);
}

static void test_gap_change_appends_chart_point() {
  TramState s{};
  applyHist(s, R"({"input_number.home_m1_tram_minutes_between_trams":[{"s":"20","lu":1793500000}]})");
  s.dirty = 0;
  applyJson(s, R"({"c":{"input_number.home_m1_tram_minutes_between_trams":{"+":{"s":"21.0","lc":1793570000.1}}}})");
  CHECK(s.histCount == 2 && s.hist[1].t == 1793570000u && s.hist[1].v == 21.0f);
  CHECK(s.dirty == (DIRTY_GAP | DIRTY_CHART));
  TramState n{};  // before any history arrived: nothing appended
  applyJson(n, R"({"c":{"input_number.home_m1_tram_minutes_between_trams":{"+":{"s":"21.0","lc":1793570000.1}}}})");
  CHECK(n.histCount == 0 && !(n.dirty & DIRTY_CHART));
}

static void test_gap_append_full_drops_oldest() {
  TramState s{};
  s.histKnown = true; s.histCount = TRAM_MAX_HIST;
  for (int i = 0; i < TRAM_MAX_HIST; i++) s.hist[i] = {1793400000u + i * 60u, (float)i};
  applyJson(s, R"({"c":{"input_number.home_m1_tram_minutes_between_trams":{"+":{"s":"7","lc":1793570000}}}})");
  CHECK(s.histCount == TRAM_MAX_HIST && s.hist[0].v == 1.0f && s.hist[TRAM_MAX_HIST - 1].v == 7.0f);
}

// Review fix 1: messages go through the same ArduinoJson filter the firmware uses.
static void applyFiltered(HaProto& p, TramState& s, const char* json) {
  JsonDocument f; haBuildFilter(f);
  JsonDocument d;
  CHECK(deserializeJson(d, json, DeserializationOption::Filter(f)) == DeserializationError::Ok);
  haHandleMessage(p, s, g_climate, g_lights, d.as<JsonVariantConst>());
}

static void test_filter_keeps_gap_timestamp() {
  HaProto p; TramState s{};
  p.histPending = 2;
  applyFiltered(p, s, R"({"id":2,"type":"result","success":true,"result":{"input_number.home_m1_tram_minutes_between_trams":[{"s":"20","lu":1793500000}]}})");
  applyFiltered(p, s, R"({"id":1,"type":"event","event":{"c":{"input_number.home_m1_tram_minutes_between_trams":{"+":{"s":"21.0","lc":1793570000.1,"c":"ctx"}}}}})");
  CHECK(s.histCount == 2 && s.hist[1].v == 21.0f);
}

static void test_filter_keeps_departures_and_removals() {
  HaProto p; TramState s{};
  applyFiltered(p, s, R"({"id":1,"type":"event","event":{"a":{"sensor.kuckhoffstr_berlin":{"s":"x","a":{"icon":"mdi:clock","departures":[
    {"line_name":"M1","time":"23:18","timestamp":"2026-11-01T23:18:00+01:00","direction":"Mitte","color":"#D82020","delay":0,"cancelled":false}]}}}}})");
  CHECK(s.depCount == 1);
  applyFiltered(p, s, R"({"id":1,"type":"event","event":{"c":{"sensor.kuckhoffstr_berlin":{"-":{"a":["departures"]}}}}})");
  CHECK(s.depCount == 0 && !s.depKnown);
}

// Review fix 3: no stale "now" forever.
static void test_departures_cleared_when_unavailable() {
  TramState s{};
  applyJson(s, INITIAL);
  applyJson(s, R"({"c":{"sensor.kuckhoffstr_berlin":{"+":{"s":"unavailable","a":{"friendly_name":"x"}}}}})");
  CHECK(s.depCount == 0 && !s.depKnown);
  applyJson(s, INITIAL);
  applyJson(s, R"({"a":{"sensor.kuckhoffstr_berlin":{"s":"unavailable","a":{"friendly_name":"x"}}}})");
  CHECK(s.depCount == 0 && !s.depKnown);
}

static void test_first_upcoming_skips_past() {
  TramState s{};
  applyJson(s, INITIAL);  // 22:58, 23:18, 23:38 local = 21:58Z, 22:18Z, 22:38Z
  CHECK(tramFirstUpcoming(s, 1793570280u - 300) == 0);
  CHECK(tramFirstUpcoming(s, 1793570280u + 30) == 0);   // within the "now" minute
  CHECK(tramFirstUpcoming(s, 1793570280u + 120) == 1);  // 22:58 has left
  CHECK(tramFirstUpcoming(s, 1793570280u + 3600) == -1);
  CHECK(tramFirstUpcoming(s, 0) == 0);                  // no clock: show the list as is
}

#include "battery_meter.h"

static void test_battery_fill() {
  CHECK(batteryFillPx(100, 18) == 18);
  CHECK(batteryFillPx(50, 18) == 9);
  CHECK(batteryFillPx(0, 18) == 0);
  CHECK(batteryFillPx(3, 18) == 1);    // anything above 0 shows at least a sliver
  CHECK(batteryFillPx(140, 18) == 18); // gauge glitches clamp
  CHECK(batteryFillPx(-5, 18) == 0);
}

static void test_battery_color() {
  CHECK(batteryColor(80) == 0x07E0);  // green
  CHECK(batteryColor(30) == 0x07E0);
  CHECK(batteryColor(29) == 0xFFE0);  // yellow
  CHECK(batteryColor(15) == 0xFFE0);
  CHECK(batteryColor(14) == 0xF800);  // red
}

#include "climate_model.h"

static void test_temp_colour() {
  // Dashboard rule: >26 red, >=20 yellow, else blue; unknown grey.
  CHECK(tempColour({20.3f, true}) == CL_YELLOW);
  CHECK(tempColour({20.0f, true}) == CL_YELLOW);
  CHECK(tempColour({19.7f, true}) == CL_BLUE);
  CHECK(tempColour({26.0f, true}) == CL_YELLOW);
  CHECK(tempColour({26.1f, true}) == CL_RED);
  CHECK(tempColour({30.0f, false}) == CL_GREY);
}

static void test_humidity_colour() {
  // Dashboard rule: >75 red, >65 yellow, else green.
  CHECK(humColour({65.71f, true}) == CL_YELLOW);
  CHECK(humColour({65.0f, true}) == CL_GREEN);
  CHECK(humColour({63.0f, true}) == CL_GREEN);
  CHECK(humColour({75.0f, true}) == CL_YELLOW);
  CHECK(humColour({75.5f, true}) == CL_RED);
  CHECK(humColour({0, false}) == CL_GREY);
}

static void test_pm_colour() {
  // Under the card's limit green, up to twice the limit yellow, above red.
  CHECK(pmColour({9.9f, true}, 10) == CL_GREEN);
  CHECK(pmColour({14.0f, true}, 10) == CL_YELLOW);
  CHECK(pmColour({20.0f, true}, 10) == CL_YELLOW);
  CHECK(pmColour({20.1f, true}, 10) == CL_RED);
  CHECK(pmColour({16.1f, true}, 20) == CL_GREEN);
  CHECK(pmColour({0, false}, 20) == CL_GREY);
}

static void test_rooms_map_to_sensors() {
  CHECK_STR(CLIMATE_ROOMS[0].name, "Bedroom");
  CHECK_STR(CLIMATE_ROOMS[0].temp, "sensor.multi_sensor_bedzimmer");
  CHECK_STR(CLIMATE_ROOMS[0].hum, "sensor.multi_sensor_bedzimmer_2");
  CHECK_STR(CLIMATE_ROOMS[1].name, "Theo's room");
  CHECK_STR(CLIMATE_ROOMS[1].temp, "sensor.babyroom");
  CHECK_STR(CLIMATE_ROOMS[1].hum, "sensor.babyroom_2");
  CHECK_STR(CLIMATE_ROOMS[2].name, "Bathroom");
  CHECK_STR(CLIMATE_ROOMS[2].temp, "sensor.temperature_9");
  CHECK_STR(CLIMATE_ROOMS[2].hum, "sensor.humidity_10");
  CHECK_STR(CLIMATE_ROOMS[3].name, "Living room");
  CHECK_STR(CLIMATE_ROOMS[3].temp, "sensor.living_room");
  CHECK_STR(CLIMATE_ROOMS[3].hum, "sensor.living_room_2");
  CHECK_STR(CLIMATE_PM[0].id, "sensor.nova_pm2_5");
  CHECK(CLIMATE_PM[0].limit == 10);
  CHECK_STR(CLIMATE_PM[1].id, "sensor.nova_pm10");
  CHECK(CLIMATE_PM[1].limit == 20);
}

static void test_climate_event() {
  ClimateState c{};
  JsonDocument d;
  deserializeJson(d, R"({"a":{"sensor.babyroom":{"s":"19.75"},"sensor.babyroom_2":{"s":"63.76"},
    "sensor.nova_pm10":{"s":"16.1"},"sensor.kuckhoffstr_berlin":{"s":"x"}}})");
  climateApplyEvent(c, d.as<JsonVariantConst>());
  CHECK(c.temp[1].known && c.temp[1].v == 19.75f);
  CHECK(c.hum[1].known && c.hum[1].v == 63.76f);
  CHECK(c.pm[1].known && c.pm[1].v == 16.1f);
  CHECK(!c.temp[0].known && !c.pm[0].known);
  CHECK(c.dirty == (CLIMATE_DIRTY_ROOM(1) | CLIMATE_DIRTY_PM(1)));
  c.dirty = 0;
  deserializeJson(d, R"({"c":{"sensor.babyroom":{"+":{"s":"unavailable"}},"sensor.living_room_2":{"+":{"lc":1.0}}}})");
  climateApplyEvent(c, d.as<JsonVariantConst>());
  CHECK(!c.temp[1].known);                 // unavailable -> grey "--"
  CHECK(!c.hum[3].known);                  // change without a state: ignored
  CHECK(c.dirty == CLIMATE_DIRTY_ROOM(1));
}

static void test_climate_format() {
  char b[12];
  climateFormat({20.88f, true}, 1, b, sizeof(b)); CHECK_STR(b, "20.9");
  climateFormat({65.71f, true}, 0, b, sizeof(b)); CHECK_STR(b, "66");
  climateFormat({0, false}, 1, b, sizeof(b)); CHECK_STR(b, "--");
}

static void test_subscribe_lists_all_entities() {
  char buf[700];
  CHECK(haBuildSubscribe(buf, sizeof(buf)));
  CHECK(strstr(buf, "\"type\":\"subscribe_entities\"") != nullptr);
  CHECK(strstr(buf, "\"sensor.kuckhoffstr_berlin\"") != nullptr);
  CHECK(strstr(buf, "\"sensor.home_m1_tram_last_departed\"") != nullptr);
  for (const ClimateRoom& r : CLIMATE_ROOMS) {
    std::string t = std::string("\"") + r.temp + "\"", h = std::string("\"") + r.hum + "\"";
    CHECK(strstr(buf, t.c_str()) && strstr(buf, h.c_str()));
  }
  CHECK(strstr(buf, "\"sensor.nova_pm2_5\"") && strstr(buf, "\"sensor.nova_pm10\""));
  JsonDocument d;
  CHECK(deserializeJson(d, buf) == DeserializationError::Ok);  // valid JSON
  CHECK(d["entity_ids"].size() == 16);
  for (const LightDef& l : LIGHTS) {
    std::string q = std::string("\"") + l.id + "\"";
    CHECK(strstr(buf, q.c_str()) != nullptr);
  }
  char tiny[40];
  CHECK(!haBuildSubscribe(tiny, sizeof(tiny)));  // reports truncation
}

static void test_routing_reaches_climate() {
  HaProto p; TramState s{}; ClimateState c{};
  JsonDocument f; haBuildFilter(f);
  JsonDocument d;
  deserializeJson(d, R"({"id":1,"type":"event","event":{"c":{"sensor.living_room":{"+":{"s":"20.88","lc":1793570000.1}}}}})",
                  DeserializationOption::Filter(f));
  LightsState L{};
  haHandleMessage(p, s, c, L, d.as<JsonVariantConst>());
  CHECK(c.temp[3].known && c.temp[3].v == 20.88f);
}

#include "lights_model.h"

static void applyLights(LightsState& L, const char* json) {
  JsonDocument f; haBuildFilter(f);
  JsonDocument d;
  CHECK(deserializeJson(d, json, DeserializationOption::Filter(f)) == DeserializationError::Ok);
  HaProto p; TramState t{}; ClimateState c{};
  haHandleMessage(p, t, c, L, d.as<JsonVariantConst>());
}

static void test_lights_defs() {
  CHECK_STR(LIGHTS[0].name, "Philips lamp"); CHECK_STR(LIGHTS[0].id, "light.philips_lamp");
  CHECK_STR(LIGHTS[1].name, "Tripod lamp");  CHECK_STR(LIGHTS[1].id, "light.extended_color_light_10");
  CHECK_STR(LIGHTS[2].name, "Moon lamp");    CHECK_STR(LIGHTS[2].id, "light.extended_color_light_8");
}

static void test_lights_state_from_events() {
  LightsState L{};
  // Full state through the real message filter (extra attributes dropped).
  applyLights(L, R"({"id":1,"type":"event","event":{"a":{
    "light.philips_lamp":{"s":"on","a":{"brightness":128,"color_mode":"color_temp","color_temp_kelvin":2702,"hs_color":[28.4,65.6],"effect_list":["a"]}},
    "light.extended_color_light_10":{"s":"on","a":{"brightness":255,"color_mode":"xy","hs_color":[231.0,98.0]}},
    "light.extended_color_light_8":{"s":"unavailable","a":{}}}}})");
  const LightState& a = L.l[0];
  CHECK(a.known && a.available && a.on && a.brightness == 128 && a.mode == LM_CT && a.kelvin == 2702);
  const LightState& b = L.l[1];
  CHECK(b.on && b.mode == LM_HS && b.hue == 231.0f && b.sat == 98.0f);
  CHECK(L.l[2].known && !L.l[2].available && !L.l[2].on);
  CHECK(L.dirty == 7);
  L.dirty = 0;
  // Turning off: HA sends nulls for the colour attributes.
  applyLights(L, R"({"id":1,"type":"event","event":{"c":{"light.philips_lamp":{"+":{"s":"off","a":{"brightness":null,"color_mode":null}}}}}})");
  CHECK(!L.l[0].on && L.l[0].available && L.dirty == 1);
  // Brightness-only change keeps the state.
  applyLights(L, R"({"id":1,"type":"event","event":{"c":{"light.extended_color_light_10":{"+":{"a":{"brightness":51}}}}}})");
  CHECK(L.l[1].on && L.l[1].brightness == 51);
}

static void test_brightness_pct_and_steps() {
  CHECK(lightPct(255) == 100 && lightPct(128) == 50 && lightPct(3) == 1 && lightPct(0) == 0);
  CHECK(lightStepPct(50, true) == 60);
  CHECK(lightStepPct(55, true) == 60);
  CHECK(lightStepPct(100, true) == 100);
  CHECK(lightStepPct(0, true) == 10);
  CHECK(lightStepPct(50, false) == 40);
  CHECK(lightStepPct(55, false) == 50);
  CHECK(lightStepPct(10, false) == 1);
  CHECK(lightStepPct(1, false) == 1);
}

static void test_colour_preset_match() {
  LightState s{}; s.known = s.available = s.on = true;
  s.mode = LM_CT; s.kelvin = 2702;
  CHECK_STR(COLOUR_PRESETS[lightPresetIndex(s)].name, "Warm white");
  s.kelvin = 6400;
  CHECK_STR(COLOUR_PRESETS[lightPresetIndex(s)].name, "Cool white");
  s.kelvin = 5200;
  CHECK(lightPresetIndex(s) == -1);           // between presets: "Custom"
  s.mode = LM_HS; s.hue = 231; s.sat = 98;
  CHECK_STR(COLOUR_PRESETS[lightPresetIndex(s)].name, "Blue");
  s.hue = 355; s.sat = 90;
  CHECK_STR(COLOUR_PRESETS[lightPresetIndex(s)].name, "Red");  // hue wraps
  s.sat = 20;
  CHECK(lightPresetIndex(s) == -1);
}

static void test_lights_keys_navigation() {
  LightsState L{}; L.l[0].known = L.l[0].available = true;
  LightAction a;
  CHECK(lightsOnKey(L, KEY_JOY_DOWN, a) && L.sel == 1 && a.kind == LA_NONE);
  CHECK(lightsOnKey(L, KEY_JOY_DOWN, a) && L.sel == 2);
  CHECK(lightsOnKey(L, KEY_JOY_DOWN, a) && L.sel == 2);      // clamps
  CHECK(lightsOnKey(L, KEY_JOY_UP, a) && L.sel == 1);
  CHECK(!lightsOnKey(L, KEY_JOY_RIGHT, a));                  // list: page switching
  CHECK(!lightsOnKey(L, KEY_A, a));
  CHECK(lightsOnKey(L, KEY_JOY_PRESS, a) && L.editing && L.field == LF_POWER);
  CHECK(lightsOnKey(L, KEY_JOY_DOWN, a) && L.field == LF_BRIGHTNESS);
  CHECK(lightsOnKey(L, KEY_JOY_DOWN, a) && L.field == LF_COLOUR);
  CHECK(lightsOnKey(L, KEY_JOY_DOWN, a) && L.field == LF_COLOUR);
  CHECK(lightsOnKey(L, KEY_JOY_LEFT, a) && L.editing);       // editing: left/right consumed
  CHECK(lightsOnKey(L, KEY_JOY_PRESS, a) && !L.editing);
  L.editing = true;
  CHECK(lightsOnKey(L, KEY_B, a) && !L.editing);             // B also goes back
  CHECK(L.uiDirty);
}

static void test_lights_keys_actions() {
  LightsState L{};
  LightState& s = L.l[1];
  s.known = s.available = true; s.on = true; s.brightness = 128; s.mode = LM_CT; s.kelvin = 2700;
  L.sel = 1; L.editing = true; L.field = LF_POWER;
  LightAction a;
  lightsOnKey(L, KEY_JOY_RIGHT, a);
  CHECK(a.kind == LA_TOGGLE && a.light == 1);
  L.field = LF_BRIGHTNESS;
  lightsOnKey(L, KEY_JOY_RIGHT, a);
  CHECK(a.kind == LA_BRIGHTNESS && a.pct == 60);
  lightsOnKey(L, KEY_JOY_LEFT, a);
  CHECK(a.kind == LA_BRIGHTNESS && a.pct == 40);
  L.field = LF_COLOUR;
  lightsOnKey(L, KEY_JOY_RIGHT, a);
  CHECK(a.kind == LA_COLOUR && a.preset == 1);               // warm white -> next
  lightsOnKey(L, KEY_JOY_LEFT, a);
  CHECK(a.kind == LA_COLOUR && a.preset == COLOUR_PRESET_COUNT - 1);  // wraps
  s.available = false;
  lightsOnKey(L, KEY_JOY_RIGHT, a);
  CHECK(a.kind == LA_NONE);                                  // unavailable: no calls
}

static void test_light_call_json() {
  char buf[256];
  JsonDocument d;
  LightAction a{LA_TOGGLE, 0, 0, 0};
  CHECK(lightBuildCall(buf, sizeof(buf), 42, a));
  deserializeJson(d, buf);
  CHECK(d["id"] == 42 && d["type"] == "call_service" && d["domain"] == "light" && d["service"] == "toggle");
  CHECK(d["target"]["entity_id"] == "light.philips_lamp");
  a = {LA_BRIGHTNESS, 2, 60, 0};
  lightBuildCall(buf, sizeof(buf), 43, a); deserializeJson(d, buf);
  CHECK(d["service"] == "turn_on" && d["service_data"]["brightness_pct"] == 60);
  CHECK(d["target"]["entity_id"] == "light.extended_color_light_8");
  a = {LA_COLOUR, 1, 0, 0};  // Warm white
  lightBuildCall(buf, sizeof(buf), 44, a); deserializeJson(d, buf);
  CHECK(d["service_data"]["color_temp_kelvin"] == 2700 && d["service_data"]["hs_color"].isNull());
  int blue = -1;
  for (int i = 0; i < COLOUR_PRESET_COUNT; i++) if (!strcmp(COLOUR_PRESETS[i].name, "Blue")) blue = i;
  a = {LA_COLOUR, 1, 0, (uint8_t)blue};
  lightBuildCall(buf, sizeof(buf), 45, a); deserializeJson(d, buf);
  CHECK(d["service_data"]["hs_color"][0] == 230 && d["service_data"]["hs_color"][1] == 100);
  CHECK(!lightBuildCall(buf, 20, 46, a));                    // too small
}

static void test_light_optimistic_update() {
  LightsState L{};
  LightState& s = L.l[0];
  s.known = s.available = true; s.on = false; s.brightness = 0;
  lightsApplyAction(L, {LA_BRIGHTNESS, 0, 60, 0});
  CHECK(s.on && lightPct(s.brightness) == 60 && (L.dirty & 1));
  lightsApplyAction(L, {LA_TOGGLE, 0, 0, 0});
  CHECK(!s.on);
  lightsApplyAction(L, {LA_COLOUR, 0, 0, 0});               // colour change turns it on
  CHECK(s.on && s.mode == LM_CT && s.kelvin == COLOUR_PRESETS[0].kelvin);
}

static void test_light_swatch() {
  LightState s{}; s.known = s.available = s.on = true;
  s.mode = LM_HS; s.hue = 0; s.sat = 100;
  CHECK(lightSwatch565(s) == 0xF800);                        // pure red
  s.hue = 120;
  CHECK(lightSwatch565(s) == 0x07E0);
  s.on = false;
  CHECK(lightSwatch565(s) == 0);                             // 0 = draw as off
}

int main() {
  test_ws_encode_small();
  test_ws_encode_medium();
  test_ws_parse_server_frames();
  test_ws_mask_roundtrip();
  test_parse_iso();
  test_berlin_offset();
  test_format_clock();
  test_countdown_clamps();
  test_unsynced_time();
  test_format_ago();
  test_initial_event();
  test_gap_change_only();
  test_departures_change();
  test_empty_and_cancelled();
  test_state_only_change_keeps_departures();
  test_gap_unavailable_and_unknown_entity();
  test_history_skips_non_numeric();
  test_history_keeps_latest_96();
  test_hex_to_565();
  test_auth_flow();
  test_auth_invalid_rejects();
  test_event_routing();
  test_history_result_routing();
  test_pong_tracking();
  test_backoff();
  test_format_iso_utc();
  test_pick_skips_unknown_security();
  test_pick_strongest_supported();
  test_pick_none();
  test_chart_scale();
  test_chart_mapping();
  test_sleep_button_c_toggles();
  test_sleep_other_keys_wake_only();
  test_gap_change_appends_chart_point();
  test_gap_append_full_drops_oldest();
  test_filter_keeps_gap_timestamp();
  test_filter_keeps_departures_and_removals();
  test_departures_cleared_when_unavailable();
  test_first_upcoming_skips_past();
  test_battery_fill();
  test_battery_color();
  test_temp_colour();
  test_humidity_colour();
  test_pm_colour();
  test_rooms_map_to_sensors();
  test_climate_event();
  test_climate_format();
  test_subscribe_lists_all_entities();
  test_routing_reaches_climate();
  test_lights_defs();
  test_lights_state_from_events();
  test_brightness_pct_and_steps();
  test_colour_preset_match();
  test_lights_keys_navigation();
  test_lights_keys_actions();
  test_light_call_json();
  test_light_optimistic_update();
  test_light_swatch();
  printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
