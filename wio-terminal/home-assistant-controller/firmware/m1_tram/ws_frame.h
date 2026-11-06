// RFC 6455 frame header helpers. Pure functions, host-tested (test/run.sh).
#pragma once
#include <stdint.h>
#include <stddef.h>

enum : uint8_t { WS_OP_CONT = 0x0, WS_OP_TEXT = 0x1, WS_OP_BIN = 0x2,
                 WS_OP_CLOSE = 0x8, WS_OP_PING = 0x9, WS_OP_PONG = 0xA };

struct WsHeader {
  bool fin;
  uint8_t opcode;
  bool masked;
  uint64_t len;
  uint8_t mask[4];
};

// Client-to-server header (always masked). Returns bytes written (6, 8 or 14).
inline size_t wsEncodeHeader(uint8_t* out, uint8_t opcode, size_t len, const uint8_t mask[4]) {
  size_t n = 0;
  out[n++] = 0x80 | (opcode & 0x0F);
  if (len < 126) {
    out[n++] = 0x80 | (uint8_t)len;
  } else if (len <= 0xFFFF) {
    out[n++] = 0x80 | 126;
    out[n++] = (uint8_t)(len >> 8);
    out[n++] = (uint8_t)len;
  } else {
    out[n++] = 0x80 | 127;
    uint64_t l = len;
    for (int i = 7; i >= 0; i--) out[n++] = (uint8_t)(l >> (i * 8));
  }
  for (int i = 0; i < 4; i++) out[n++] = mask[i];
  return n;
}

// Returns header length once `avail` bytes cover it, 0 if more bytes are needed.
inline size_t wsParseHeader(const uint8_t* in, size_t avail, WsHeader& h) {
  if (avail < 2) return 0;
  h.fin = in[0] & 0x80;
  h.opcode = in[0] & 0x0F;
  h.masked = in[1] & 0x80;
  uint8_t l7 = in[1] & 0x7F;
  size_t n = 2;
  if (l7 == 126) {
    if (avail < 4) return 0;
    h.len = ((uint64_t)in[2] << 8) | in[3];
    n = 4;
  } else if (l7 == 127) {
    if (avail < 10) return 0;
    h.len = 0;
    for (int i = 0; i < 8; i++) h.len = (h.len << 8) | in[2 + i];
    n = 10;
  } else {
    h.len = l7;
  }
  if (h.masked) {
    if (avail < n + 4) return 0;
    for (int i = 0; i < 4; i++) h.mask[i] = in[n + i];
    n += 4;
  }
  return n;
}

// XOR-mask in place; `offset` is the position of data[0] within the payload.
inline void wsMask(uint8_t* data, size_t n, const uint8_t mask[4], size_t offset) {
  for (size_t i = 0; i < n; i++) data[i] ^= mask[(offset + i) & 3];
}
