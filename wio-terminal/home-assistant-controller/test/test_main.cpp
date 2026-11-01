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

int main() {
  test_ws_encode_small();
  test_ws_encode_medium();
  test_ws_parse_server_frames();
  test_ws_mask_roundtrip();
  printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
