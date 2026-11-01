// Minimal WebSocket client (RFC 6455) over rpcWiFi's WiFiClient.
// ArduinoWebsockets has no SAMD51 network backend, so this replaces it.
// Plain ws:// only, text messages up to WS_MSG_MAX bytes (larger ones are
// dropped and counted), ping -> pong, close. connect() blocks up to its
// timeout; poll() reads at most WS_POLL_BUDGET bytes per call.
#pragma once
#include <rpcWiFi.h>
#include "ws_frame.h"

#ifndef WS_MSG_MAX
#define WS_MSG_MAX 24576
#endif
#define WS_POLL_BUDGET 4096

class WsClient {
 public:
  bool connect(const char* host, uint16_t port, const char* path, uint32_t timeoutMs = 5000) {
    close();
    if (!c_.connect(host, port)) return false;
    c_.print("GET "); c_.print(path); c_.print(" HTTP/1.1\r\nHost: ");
    c_.print(host); c_.print(":"); c_.print(port);
    // The server doesn't check the key's randomness; a fixed valid key is fine on the LAN.
    c_.print("\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n\r\n");
    // Read the response headers up to the blank line; status must be 101.
    char line[64]; size_t ll = 0; bool first = true, ok = false;
    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
      if (!c_.available()) { if (!c_.connected()) break; delay(2); continue; }
      int ch = c_.read();
      if (ch < 0) continue;
      if (ch == '\n') {
        line[ll] = 0;
        if (ll && line[ll - 1] == '\r') line[--ll] = 0;
        if (first) { ok = strstr(line, " 101") != nullptr; first = false; }
        else if (ll == 0) { open_ = ok; break; }
        ll = 0;
      } else if (ll < sizeof(line) - 1) {
        line[ll++] = (char)ch;
      }
    }
    if (!open_) c_.stop();
    resetParser();
    return open_;
  }

  bool connected() { return open_ && c_.connected(); }

  bool sendText(const char* s, size_t n) {
    if (!connected()) return false;
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) mask[i] = (uint8_t)random(256);
    uint8_t hdr[14];
    size_t hn = wsEncodeHeader(hdr, WS_OP_TEXT, n, mask);
    if (c_.write(hdr, hn) != hn) return false;
    uint8_t tmp[256];
    for (size_t off = 0; off < n; off += sizeof(tmp)) {
      size_t k = n - off < sizeof(tmp) ? n - off : sizeof(tmp);
      memcpy(tmp, s + off, k);
      wsMask(tmp, k, mask, off);
      if (c_.write(tmp, k) != k) return false;
    }
    return true;
  }
  bool sendText(const char* s) { return sendText(s, strlen(s)); }

  // True when a complete text message is ready in msg() (null-terminated,
  // valid until the next poll()).
  bool poll() {
    if (!open_) return false;
    if (!c_.connected() && chunkPos_ >= chunkLen_ && !c_.available()) { open_ = false; return false; }
    size_t budget = WS_POLL_BUDGET;
    while (true) {
      if (chunkPos_ >= chunkLen_) {
        if (budget == 0) return false;
        int av = c_.available();
        if (av <= 0) return false;
        size_t want = (size_t)av < sizeof(chunk_) ? (size_t)av : sizeof(chunk_);
        if (want > budget) want = budget;
        int got = c_.read(chunk_, want);
        if (got <= 0) return false;
        chunkLen_ = got; chunkPos_ = 0; budget -= got;
      }
      if (consume()) return true;
    }
  }

  char* msg() { return buf_; }
  size_t msgLen() const { return msgLen_; }
  uint32_t dropped() const { return dropped_; }

  void close() {
    if (open_ && c_.connected()) {
      uint8_t hdr[14]; const uint8_t mask[4] = {0, 0, 0, 0};
      size_t hn = wsEncodeHeader(hdr, WS_OP_CLOSE, 0, mask);
      c_.write(hdr, hn);
    }
    c_.stop();
    open_ = false;
    resetParser();
  }

 private:
  void resetParser() {
    hdrHave_ = 0; inPayload_ = false; payLeft_ = 0; payOff_ = 0;
    msgLen_ = 0; dropping_ = false; chunkLen_ = chunkPos_ = 0;
  }

  // Processes buffered bytes; returns true when a text message completes.
  bool consume() {
    while (chunkPos_ < chunkLen_) {
      if (!inPayload_) {
        hdr_[hdrHave_++] = chunk_[chunkPos_++];
        size_t hn = wsParseHeader(hdr_, hdrHave_, h_);
        if (hn == 0) {
          if (hdrHave_ >= sizeof(hdr_)) { close(); return false; }  // malformed
          continue;
        }
        hdrHave_ = 0; inPayload_ = true; payLeft_ = h_.len; payOff_ = 0;
        if (h_.opcode >= 0x8) {
          ctrlLen_ = 0;
          if (h_.len > sizeof(ctrl_)) { close(); return false; }
        } else if (h_.opcode != WS_OP_CONT) {
          msgOp_ = h_.opcode; msgLen_ = 0; dropping_ = false;
        }
      } else {
        size_t k = chunkLen_ - chunkPos_;
        if (k > payLeft_) k = (size_t)payLeft_;
        const uint8_t* src = chunk_ + chunkPos_;
        if (h_.opcode >= 0x8) {
          memcpy(ctrl_ + ctrlLen_, src, k); ctrlLen_ += k;
        } else if (!dropping_) {
          if (msgLen_ + k > WS_MSG_MAX) { dropping_ = true; }
          else { memcpy(buf_ + msgLen_, src, k); msgLen_ += k; }
        }
        if (h_.masked) {  // servers never mask, but stay correct
          uint8_t* dst = h_.opcode >= 0x8 ? ctrl_ + ctrlLen_ - k : (uint8_t*)buf_ + msgLen_ - k;
          if (h_.opcode >= 0x8 || !dropping_) wsMask(dst, k, h_.mask, payOff_);
        }
        chunkPos_ += k; payLeft_ -= k; payOff_ += k;
      }
      if (inPayload_ && payLeft_ == 0 && frameDone()) return true;
    }
    return false;
  }

  bool frameDone() {
    inPayload_ = false;
    if (h_.opcode == WS_OP_PING) {
      uint8_t hdr[14]; uint8_t mask[4] = {0, 0, 0, 0};
      size_t hn = wsEncodeHeader(hdr, WS_OP_PONG, ctrlLen_, mask);
      c_.write(hdr, hn);
      if (ctrlLen_) c_.write(ctrl_, ctrlLen_);
      return false;
    }
    if (h_.opcode == WS_OP_CLOSE) { close(); return false; }
    if (h_.opcode == WS_OP_PONG || !h_.fin) return false;
    if (dropping_) { dropped_++; dropping_ = false; return false; }
    if (msgOp_ != WS_OP_TEXT) return false;
    buf_[msgLen_] = 0;
    return true;
  }

  WiFiClient c_;
  bool open_ = false;
  uint8_t hdr_[14]; size_t hdrHave_ = 0;
  WsHeader h_{};
  bool inPayload_ = false;
  uint64_t payLeft_ = 0; size_t payOff_ = 0;
  uint8_t ctrl_[125]; size_t ctrlLen_ = 0;
  uint8_t msgOp_ = WS_OP_TEXT;
  char buf_[WS_MSG_MAX + 1]; size_t msgLen_ = 0; bool dropping_ = false;
  uint32_t dropped_ = 0;
  uint8_t chunk_[1024]; size_t chunkLen_ = 0, chunkPos_ = 0;
};
