// ring.h — fixed-capacity history (oldest first, newest at v[len-1]) for the new sensor
// pages. 288 samples at one per 5 min = 24 h, the same window as the existing charts.
#pragma once
#include <string.h>

template <typename T, int N>
struct Ring {
  T   v[N];
  int len = 0;
  void push(T x) {
    if (len < N) { v[len++] = x; return; }
    memmove(v, v + 1, sizeof(T) * (N - 1));
    v[N - 1] = x;
  }
};

const int SENSOR_HIST_N = 288;
