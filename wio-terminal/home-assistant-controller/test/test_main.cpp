#include <cstdio>
#include <cstring>
#include <cstdint>

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
  printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
