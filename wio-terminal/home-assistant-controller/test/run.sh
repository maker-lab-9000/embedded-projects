#!/bin/sh
# Host-side unit tests for the pure-logic headers in firmware/m1_tram.
set -e
cd "$(dirname "$0")/.."
OUT="${TMPDIR:-/tmp}/tram_tests"
clang++ -std=c++17 -Wall -Wextra -I firmware/m1_tram \
  -I "$HOME/Documents/Arduino/libraries/ArduinoJson/src" \
  test/test_main.cpp -o "$OUT"
"$OUT"
