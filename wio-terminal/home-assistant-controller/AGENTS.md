# Agent rules: home-assistant-controller

- **Never retry a rejected Home Assistant token.** HA bans the client IP after
  a few failed logins. When testing against HA, make one attempt; if it fails,
  stop and report. Don't loop curl or firmware uploads against a bad token.
- **No secrets in git.** Token and WiFi live in `firmware/m1_tram/secrets.h`
  (gitignored); `.env` holds a token too. Never print them.
- **Non-blocking `loop()`.** Modules follow `xxxInit()` + `xxxPoll()`. Only a
  (backed-off) WiFi scan/join or websocket connect may block.
- **Pure logic goes in host-testable headers** (no Arduino includes) with tests
  in `test/test_main.cpp`; run `test/run.sh` before every commit.
- **Serial on this core:** `Serial.printf` truncates around 80 chars; build
  long lines from several prints. Reading the port from scripts: arduino-cli
  monitor exits when stdin isn't a TTY; use a raw reader and kill stale
  monitors before uploading.
- **Adding a page:** a pure `<name>_model.h` (entities, state, apply
  function, tested), `page_<name>.h` using `drawHeader()` from `header.h`,
  its entity ids in `haBuildSubscribe()` and its apply call in
  `haHandleMessage()` (`ha_proto.h`), and a row in `PAGES[]` in
  `m1_tram.ino`.
- **`arduino-cli upload` doesn't compile.** Run `arduino-cli compile` first
  or you flash the previous build.
- **Serial port is not a neutral observer.** Reopening the port in a loop
  makes rpcWiFi calls hang (the watchdog then resets the board). For
  stability tests, upload, then watch `/dev/cu.usbmodem*` disappearing
  (= resets) without opening it.
- **Don't drive `LCD_BACKLIGHT` with digitalWrite**; use `backlightSet()`.
- **Check the layout without the device in hand:** send `p` (next page) and `s` over serial in one port session and
  convert the RGB565 dump (big-endian) to PNG.
