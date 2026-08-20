# Home Assistant Controller (M5StickS3)

An M5StickS3 that connects to Home Assistant over a WebSocket, shows live entity
states on the LCD, and toggles a light on/off. A **Controls** page toggles
entities; a **Dashboard** page shows read-only values. Which entities appear is
defined entirely in a gitignored `.env`, so changing them is an edit + regen +
reflash — no code changes.

**Maintainer:** George Babanau · actively maintained (Aug 2026)

## Hardware

- M5StickS3 (ESP32-S3, 1.14" 135×240 LCD, 250 mAh battery).
- 2.4 GHz WiFi reachable from a running Home Assistant instance. No wiring.

## Setup

1. Copy the template and fill in your values:
   ```sh
   cp m5sticks3/ha-controller/.env.example m5sticks3/ha-controller/.env
   ```
   Edit `.env` (gitignored):
   - `WIFI_SSID` / `WIFI_PASSWORD` — your 2.4 GHz network.
   - `HA_HOST` — HA hostname or **IP only** (no `http://`, no `:port`). If
     `homeassistant.local` doesn't resolve on your network, use the IP.
   - `HA_PORT` — usually `8123`.
   - `HA_TOKEN` — a long-lived access token: HA → your profile → Security →
     Long-lived access tokens → Create Token.
   - `LIGHT_ENTITY` — the light's full `entity_id` (e.g. `light.living_room`),
     found in HA → Developer Tools → States. `LIGHT_LABEL` — its screen name.
   - `DISPLAY_ENTITIES` — dashboard entities, `;`-separated, each
     `entity_id|Label|unit` (unit may be empty).
2. Generate the config headers (turns `.env` into the `secrets.h` /
   `generated_entities.h` the firmware compiles against):
   ```sh
   python3 m5sticks3/ha-controller/tools/gen_config.py
   ```

## Firmware

| Sketch | Purpose |
|--------|---------|
| `firmware/m1_link`       | serial-only: WiFi + WebSocket auth + state subscribe, plus a `TOGGLE` serial command (bring-up/validation) |
| `firmware/m2_controller` | the full two-page UI ← **current** |

Build and flash (regenerate first if `.env` changed):

```sh
python3 m5sticks3/ha-controller/tools/gen_config.py
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB" firmware/m2_controller
arduino-cli upload  --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,PartitionScheme=default_8MB" -p /dev/cu.usbmodemXXX firmware/m2_controller
```

Requires the `esp32:esp32` core and the ArduinoWebsockets, ArduinoJson, and
M5Unified libraries. Native-USB ESP32-S3 serial quirks (shared with the sibling
projects): **opening the serial port resets the device**; after a flash the chip
parks in download mode — **single-click the side button to boot**; if it sits
dark with USB product "ESP32_S3" it's in download mode (single-click to boot);
if a flash fails to connect, long-press the side button to force download mode,
then retry.

## Configuring entities

Everything user-specific lives in `.env`. To change what's controlled or shown:
edit `.env`, rerun `python3 tools/gen_config.py`, recompile, reflash. The
control light is `LIGHT_ENTITY`/`LIGHT_LABEL`; the dashboard is
`DISPLAY_ENTITIES` (the `id|label|unit;...` list). Adding more control entities
later is a data change — the entity list is an array, not code.

## Controls

- **KEY1 click** (front button) — move the highlight on the Controls page.
- **KEY2 click** (small button by the side) — toggle the highlighted entity.
- **KEY1 long-press (~3 s)** — switch between the Controls and Dashboard pages
  (chirps on switch).
- Side button — system reset (single-click boots, double-click powers off,
  long-press enters download mode).

## Display

240×135 landscape. The header shows the page name, a **connection dot** (green =
authenticated, yellow = connecting, red = disconnected/offline), and battery %.

- **Controls page:** each control entity with its live state — `ON` (green) /
  `OFF` (grey) / `...` (unknown). The highlighted row is boxed. While
  disconnected the controls are inert and an "offline" note shows — the state
  shown is always Home Assistant's truth (the screen updates from HA's
  `state_changed` echo, never optimistically on a button press).
- **Dashboard page:** read-only entities as `Label: value unit`, live.

Auto-reconnects with the status dot reflecting the current phase; on reconnect
it re-reads states so the display re-syncs. To keep the UI smooth it redraws
only when a *configured* entity changes, not on the whole-house event stream.

## Security

`.env`, the generated `secrets.h`, and `generated_entities.h` are **gitignored
and must never be committed** — they hold your WiFi password and HA token. Only
`.env.example` (placeholders) is committed. The connection uses plain `ws://`
on the LAN, so the token travels unencrypted on your local network — use this
on a trusted network only; TLS (`wss://`) is future work.

## Repo layout

```
firmware/   Arduino sketches (m1 link, m2 controller)
tools/      gen_config.py — turns .env into the compiled-in headers
```
