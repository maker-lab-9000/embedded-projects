// Time parsing and formatting. Pure functions, host-tested (test/run.sh).
// Epoch seconds are UTC; 0 means "unknown / clock not synced".
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
inline int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);
  const uint32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

inline bool readDigits(const char*& p, int n, int32_t& out) {
  out = 0;
  for (int i = 0; i < n; i++, p++) {
    if (*p < '0' || *p > '9') return false;
    out = out * 10 + (*p - '0');
  }
  return true;
}

// "YYYY-MM-DDTHH:MM:SS[.ffffff](Z|+HH:MM|-HH:MM)"; a space is accepted for 'T'.
inline bool parseIso8601(const char* s, uint32_t& epochUtc) {
  if (!s) return false;
  const char* p = s;
  int32_t Y, M, D, h, m, sec;
  if (!readDigits(p, 4, Y) || *p++ != '-') return false;
  if (!readDigits(p, 2, M) || *p++ != '-') return false;
  if (!readDigits(p, 2, D) || (*p != 'T' && *p != ' ')) return false;
  p++;
  if (!readDigits(p, 2, h) || *p++ != ':') return false;
  if (!readDigits(p, 2, m) || *p++ != ':') return false;
  if (!readDigits(p, 2, sec)) return false;
  if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
  int32_t off = 0;
  if (*p == '+' || *p == '-') {
    int sign = *p++ == '-' ? -1 : 1;
    int32_t oh, om;
    if (!readDigits(p, 2, oh) || *p++ != ':' || !readDigits(p, 2, om)) return false;
    off = sign * (oh * 3600 + om * 60);
  } else if (*p == 'Z') {
    p++;
  } else if (*p != 0) {
    return false;
  }
  if (M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || m > 59 || sec > 60) return false;
  int64_t e = (int64_t)daysFromCivil(Y, M, D) * 86400 + h * 3600 + m * 60 + sec - off;
  if (e <= 0 || e > 0xFFFFFFFFLL) return false;
  epochUtc = (uint32_t)e;
  return true;
}

// Epoch of the last Sunday of `month` in `year` at 01:00 UTC (EU DST switch).
inline uint32_t euSwitch(int32_t year, uint32_t month) {
  int32_t last = daysFromCivil(year, month, 31);
  int32_t wd = ((last % 7) + 11) % 7;  // 1970-01-01 was a Thursday; 0 = Sunday
  return (uint32_t)((int64_t)(last - wd) * 86400 + 3600);
}

inline int32_t yearOf(uint32_t epochUtc) {
  // Inverse of daysFromCivil, year part only.
  int32_t z = (int32_t)(epochUtc / 86400) + 719468;
  const int32_t era = z / 146097;
  const uint32_t doe = (uint32_t)(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  return (int32_t)yoe + era * 400 + (mp >= 10);
}

// Berlin UTC offset in seconds: 7200 in summer time, else 3600.
inline int32_t berlinOffset(uint32_t epochUtc) {
  int32_t y = yearOf(epochUtc);
  return (epochUtc >= euSwitch(y, 3) && epochUtc < euSwitch(y, 10)) ? 7200 : 3600;
}

inline void formatClock(uint32_t epochUtc, char* buf, size_t n) {
  if (!epochUtc) { snprintf(buf, n, "--:--"); return; }
  uint32_t local = epochUtc + berlinOffset(epochUtc);
  snprintf(buf, n, "%02u:%02u", (unsigned)(local / 3600 % 24), (unsigned)(local / 60 % 60));
}

inline void formatAgo(uint32_t now, uint32_t then, char* buf, size_t n) {
  if (!now || !then) { snprintf(buf, n, "--"); return; }
  uint32_t d = now > then ? now - then : 0;
  if (d < 60) snprintf(buf, n, "%u sec ago", (unsigned)d);
  else if (d < 3600) snprintf(buf, n, "%u min ago", (unsigned)(d / 60));
  else snprintf(buf, n, "%u h ago", (unsigned)(d / 3600));
}

inline void formatCountdown(uint32_t now, uint32_t dep, char* buf, size_t n) {
  if (!now || !dep) { snprintf(buf, n, "--"); return; }
  if (dep < now + 60) { snprintf(buf, n, "now"); return; }
  snprintf(buf, n, "in %u min", (unsigned)((dep - now) / 60));
}
