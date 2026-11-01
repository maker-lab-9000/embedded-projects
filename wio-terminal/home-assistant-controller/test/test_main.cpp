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

static HaAction handle(HaProto& p, TramState& s, const char* json) {
  JsonDocument d;
  CHECK(deserializeJson(d, json) == DeserializationError::Ok);
  return haHandleMessage(p, s, d.as<JsonVariantConst>());
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
  printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
