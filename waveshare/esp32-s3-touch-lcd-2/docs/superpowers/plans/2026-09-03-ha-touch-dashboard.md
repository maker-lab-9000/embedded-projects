# ESP32-S3-Touch-LCD-2 HA Touch Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An ESPHome firmware for the Waveshare ESP32-S3-Touch-LCD-2 that shows the user's Home Assistant climate, air-quality and homelab metrics on five LVGL pages and controls their lamps, over the HA native API.

**Architecture:** One ESPHome YAML (`esphome/lcd2.yaml`) grown task by task: base connectivity, display, touch + LVGL, data tiles, controls, idle/battery. HA entity IDs live in `substitutions:`. Values arrive via `homeassistant` sensor/binary_sensor imports; control goes out via `homeassistant.action`. esp-idf framework, octal PSRAM, `mipi_spi` display, `cst816` touch.

**Tech Stack:** ESPHome (current release, 2025.x; `mipi_spi` needs a 2025 release), Python 3.12 venv via Homebrew, LVGL (bundled in ESPHome), Home Assistant with the ESPHome integration.

**Spec:** `waveshare/esp32-s3-touch-lcd-2/docs/superpowers/specs/2026-09-03-ha-touch-dashboard-design.md`

## Global Constraints

- All commands run from the repo root `/Users/george.babanau/repos/embedded`. ESPHome lives in a venv: `~/.venvs/esphome/bin/esphome` (Task 1); the shorthand `esphome` below means that binary.
- YAML file: `waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml`. Secrets: `esphome/secrets.yaml` (git-ignored; template `secrets.yaml.example`). Never commit `secrets.yaml`.
- Pins (verbatim from Waveshare's demos): LCD MOSI 38, SCLK 39, MISO 40, CS 45, DC 42, BL 1, no RST; touch/IMU I2C SDA 48, SCL 47, CST816D at `0x15`, no INT; TF CS 41; battery ADC 5 (×3).
- Framework `esp-idf`; `psram: mode: octal, speed: 80MHz`; logger over `USB_SERIAL_JTAG`.
- Gate per task: `esphome config <yaml>` then `esphome compile <yaml>` succeed, then the hardware checkpoint. Hardware steps (USB, download mode, pressing the screen, reading HA) are the user's: stop and ask.
- First flash over USB (`--device /dev/cu.usbmodemXXXX`, board in download mode: hold BOOT, tap RESET, release BOOT); afterwards `esphome run` uses OTA.
- Commit after every task, subject prefixed `waveshare:` (docs: `waveshare docs:`). Do not push unless asked.

## File Structure

```
waveshare/esp32-s3-touch-lcd-2/
  README.md                        board facts, HA setup, how to add a tile   (Task 8 finishes it)
  .gitignore                       .esphome/, esphome/secrets.yaml, *.bin
  esphome/secrets.yaml.example     template
  esphome/secrets.yaml             real values, never committed             (Task 1)
  esphome/lcd2.yaml                the device configuration                   (Tasks 2–7)
  docs/                            photos of the two pages                    (Task 8)
  docs/superpowers/{specs,plans}/  this spec and plan
```

---

### Task 1: Toolchain and secrets

**Files:**
- Create: `waveshare/esp32-s3-touch-lcd-2/esphome/secrets.yaml` (git-ignored)

**Interfaces:**
- Produces: a working `esphome` binary (Python 3.12 venv) and filled secrets for every later task.

- [x] **Step 1: Python 3.12 and ESPHome in a venv**

```bash
brew install python@3.12
/usr/local/opt/python@3.12/bin/python3.12 -m venv ~/.venvs/esphome
~/.venvs/esphome/bin/pip install --upgrade pip esphome
~/.venvs/esphome/bin/esphome version
```

Expected: `Version: 2025.x.y` or newer. Result 2026-09-03: Python 3.12.14, ESPHome 2026.8.2. If the version is older than 2025.5, `mipi_spi` may be missing; upgrade with `pip install -U esphome`.

- [x] **Step 2: Secrets**

```bash
cd waveshare/esp32-s3-touch-lcd-2/esphome && cp secrets.yaml.example secrets.yaml
openssl rand -base64 32   # -> api_encryption_key
openssl rand -hex 16      # -> ota_password
```

Ask the user for the Wi-Fi SSID/password (2.4 GHz) and to paste them plus the two generated values into `secrets.yaml`. `ap_password` is any 8+ character string.

- [x] **Step 3: Verify nothing secret is tracked**

```bash
git -C /Users/george.babanau/repos/embedded check-ignore -v waveshare/esp32-s3-touch-lcd-2/esphome/secrets.yaml
```

Expected: the `.gitignore` rule is printed. Nothing to commit in this task (the scaffold was committed with the plan).

---

### Task 2: Base firmware — boots, joins Wi-Fi, appears in Home Assistant

**Files:**
- Create: `waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml`

**Interfaces:**
- Produces: `esphome:` node `lcd2`, `api:` with encryption, `ota:`, `wifi:` + fallback AP, `logger:` on native USB, `time: ha_time`. Every later task appends to this file.

- [x] **Step 1: Write the base configuration**

```yaml
# Waveshare ESP32-S3-Touch-LCD-2 — Home Assistant touch dashboard (ESPHome, native API).
# Pins from Waveshare's demos: LCD SPI 38/39/40, CS 45, DC 42, BL 1; touch I2C 48/47 @0x15.
substitutions:
  name: lcd2
  friendly_name: "LCD2 Dashboard"

esphome:
  name: ${name}
  friendly_name: ${friendly_name}

esp32:
  board: esp32-s3-devkitc-1
  variant: esp32s3
  flash_size: 16MB
  framework:
    type: esp-idf

psram:
  mode: octal
  speed: 80MHz

logger:
  level: INFO
  hardware_uart: USB_SERIAL_JTAG

api:
  encryption:
    key: !secret api_encryption_key

ota:
  - platform: esphome
    password: !secret ota_password

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  ap:
    ssid: "${friendly_name} Fallback"
    password: !secret ap_password

captive_portal:

time:
  - platform: homeassistant
    id: ha_time

sensor:
  - platform: wifi_signal
    name: "WiFi Signal"
    update_interval: 60s
```

- [x] **Step 2: Validate and compile**

```bash
esphome config waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
esphome compile waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
```

Expected: config prints without errors; the first compile downloads the esp-idf toolchain (several minutes) and ends with `Successfully created esp32 image`. Result 2026-09-03: ESPHome 2026.8.2, esp-idf 5.5.5, first compile ≈ 8 min incl. toolchain download; a missing closing quote in `secrets.yaml` was the only validation error.

- [x] **Step 3: Hardware checkpoint (user) — first flash over USB**

Ask the user to connect the board over USB-C, put it in download mode (hold BOOT, tap RESET, release BOOT), then:

```bash
ls /dev/cu.usbmodem*
esphome run waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml --device /dev/cu.usbmodemXXXX
```

Expected: upload, reset, then log lines `[wifi] ... Connected` and `[api] ... Address: lcd2.local`. In Home Assistant, Settings → Devices & services shows "Discovered: lcd2" → Configure → paste `api_encryption_key`. Then open the ESPHome integration entry for the device → **enable "Allow the device to perform Home Assistant actions"** (needed from Task 5 on for thermostat mode taps and Task 6 for switches). The `WiFi Signal` sensor appears in HA.

Result 2026-09-03: USB flash OK (esptool resets the S3 into the bootloader over USB-serial/JTAG, no buttons needed after the first time). First boot failed to join Wi-Fi because the SSID carried a stray `"` from an unclosed quote in `secrets.yaml`; fixed, re-flashed, then `[wifi] Connected`, `lcd2.local` pingable, API port 6053 open. HA adoption + the actions permission: user step, pending.

- [x] **Step 4: Commit**

```bash
git add waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
git commit -m "waveshare: ESPHome base config for ESP32-S3-Touch-LCD-2 (wifi, api, ota, usb logging)"
```

---

### Task 3: Display and backlight

**Files:**
- Modify: `esphome/lcd2.yaml` — add `spi:`, `output:`, `light:`, `display:` blocks

**Interfaces:**
- Produces: `display: lcd` (mipi_spi ST7789V 240×320), `light: backlight` (HA entity), `output: backlight_pwm`.

- [x] **Step 1: Append the display blocks with a test pattern**

```yaml
spi:
  id: spi_lcd
  clk_pin: GPIO39
  mosi_pin: GPIO38
  miso_pin: GPIO40

output:
  - platform: ledc
    id: backlight_pwm
    pin: GPIO1
    frequency: 1000 Hz

light:
  - platform: monochromatic
    id: backlight
    name: "Backlight"
    output: backlight_pwm
    restore_mode: ALWAYS_ON
    default_transition_length: 0s

display:
  - platform: mipi_spi
    id: lcd
    model: ST7789V
    dimensions:
      width: 240
      height: 320
    spi_id: spi_lcd
    cs_pin:
      number: GPIO45
      ignore_strapping_warning: true   # Waveshare wired LCD CS to a strapping pin; fine in practice
    dc_pin: GPIO42
    data_rate: 40MHz
    invert_colors: true      # IPS panel: Waveshare's demo constructs Arduino_ST7789 with ips=true
    color_order: bgr
    rotation: 0
    update_interval: 1s      # test pattern only; Task 4 switches to LVGL (update_interval: never)
    lambda: |-
      it.fill(Color::BLACK);
      it.filled_rectangle(0, 0, 120, 80, Color(255, 0, 0));       // top-left RED
      it.filled_rectangle(120, 0, 120, 80, Color(0, 255, 0));     // top-right GREEN
      it.filled_rectangle(0, 240, 120, 80, Color(0, 0, 255));     // bottom-left BLUE
      it.filled_rectangle(120, 240, 120, 80, Color(255, 255, 255)); // bottom-right WHITE
      it.rectangle(0, 0, 240, 320, Color(255, 255, 0));            // yellow 1-px border
```

- [x] **Step 2: Validate, compile, flash (OTA now)**

```bash
esphome run waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
```

(`run` = compile + upload; choose the OTA target when asked.)

- [x] **Step 3: Hardware checkpoint (user)**

Expected with the USB-C connector at the bottom: red top-left, green top-right, blue bottom-left, white bottom-right, a yellow border touching all four edges, black elsewhere.
- Red and blue swapped → `color_order: rgb`.
- Colours look like a photo negative (black background shows white) → `invert_colors: false`.
- Border not reaching an edge / a strip of noise → the panel offset differs: add `offset_width: 0` / `offset_height: 0` adjustments, or fall back to the legacy platform:

```yaml
display:
  - platform: st7789v
    model: Custom
    width: 240
    height: 320
    offset_width: 0
    offset_height: 0
    cs_pin: GPIO45
    dc_pin: GPIO42
    backlight_pin: no
    spi_id: spi_lcd
    rotation: 0
```

- Pattern rotated → change `rotation` (0/90/180/270) until the USB connector is at the bottom.
- The `Backlight` light entity in HA dims the panel from 0 to 100 %.

Result 2026-09-03: first OTA worked; pattern correct with `model: ST7789V`, `invert_colors: true`, `color_order: bgr`, `rotation: 0` — no changes needed. GPIO45 strapping-pin warning suppressed on `cs_pin`.

- [x] **Step 4: Commit**

```bash
git add waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
git commit -m "waveshare: ST7789 display over mipi_spi with PWM backlight and colour test pattern"
```

---

### Task 4: Touch and LVGL

**Files:**
- Modify: `esphome/lcd2.yaml` — add `i2c:`, `touchscreen:`, `lvgl:`; change the display to LVGL mode

**Interfaces:**
- Produces: `touchscreen: touch` (cst816), `lvgl:` with `page_test`. Later tasks replace `page_test` with the real pages.

- [x] **Step 1: Switch the display to LVGL mode**

In the `display:` block delete `update_interval: 1s` and the whole `lambda:`; add:

```yaml
    auto_clear_enabled: false
    update_interval: never
```

- [x] **Step 2: Append touch and a test page**

```yaml
i2c:
  id: i2c_tp
  sda: GPIO48
  scl: GPIO47
  frequency: 400kHz

touchscreen:
  - platform: cst816
    id: touch
    i2c_id: i2c_tp
    address: 0x15
    display: lcd
    update_interval: 50ms
    on_touch:
      - lambda: 'ESP_LOGI("touch", "x=%d y=%d", touch.x, touch.y);'

lvgl:
  displays: [lcd]
  touchscreens: [touch]
  buffer_size: 100%
  default_font: montserrat_20
  pages:
    - id: page_test
      bg_color: 0x000000
      widgets:
        - label:
            id: lbl_test
            align: CENTER
            text_color: 0xFFFFFF
            text: "Touch me"
        - button:
            id: btn_test
            x: 70
            y: 220
            width: 100
            height: 50
            widgets:
              - label:
                  align: CENTER
                  text: "Tap"
            on_click:
              - lvgl.label.update:
                  id: lbl_test
                  text: "Tapped!"
```

- [x] **Step 3: Validate, compile, flash**

```bash
esphome run waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
```

Then keep the log open: `esphome logs waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml`.

- [x] **Step 4: Hardware checkpoint (user)**

Expected: "Touch me" centred, a "Tap" button near the bottom. Touching logs `x= y=` with x growing left→right (0..239) and y top→bottom (0..319). Tapping the button changes the label to "Tapped!".
- Log shows `Failed to read chip id` / no touches → add `skip_probe: true` under the touchscreen.
- Coordinates mirrored or swapped → add under the touchscreen:

```yaml
    transform:
      mirror_x: false
      mirror_y: false
      swap_xy: false
```

and flip the one that fixes it (mirror_x if x runs right→left, mirror_y if y runs bottom→top, swap_xy if a horizontal finger move changes y).

Result 2026-09-03: passed without transform or skip_probe. Corners read TL (36–68, 14–39), TR (186–212, 6–55), BL (13–23, 317), BR (195–199, 295–310); button taps (120–137, 224–245); "Tapped!" appeared. Boot log shows one `i2c.idf: Performing bus recovery` (harmless) and a one-off `lvgl took a long time (56 ms)` on first render. A phantom sample at (63, 319) recurs, likely a release artefact of the CST816D — keep interactive widgets a few px away from the very bottom edge.

- [x] **Step 5: Commit**

```bash
git add waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
git commit -m "waveshare: CST816 touch and LVGL test page"
```

---

### Task 5: Header, navigation and the Home page (climate + air quality)

Revised 2026-09-03 after the user supplied their entities: five pages instead of two, navigated with `<` / `>` buttons, page title in the header. Pages: Home (thermostats + PM), Lights, Server, VMs, Storage.

**Files:**
- Modify: `esphome/lcd2.yaml` — `substitutions:`, `sensor:`/`text_sensor:` imports, `lvgl:` (style_definitions, top_layer, pages)

**Interfaces:**
- Consumes: `ha_time`, `lcd`, `touch`.
- Produces: header ids `lbl_time`, `lbl_title`, `lbl_link`; page ids `page_home`, `page_lights`, `page_server`, `page_vms`, `page_storage` (later pages are placeholders until their task); style ids `tile_style`, `row_style`, `nav_style`; the *severity pattern* (an `if` chain colouring a value label green/yellow/red) reused by Tasks 7–8.

- [x] **Step 1: Substitutions**

```yaml
  # --- Home page ---
  thermo1_entity: climate.thermostat_14
  thermo1_name: "Theo's room"
  thermo2_entity: climate.thermostat_15
  thermo2_name: "Living Room"
  pm25_entity: sensor.nova_pm2_5
  pm10_entity: sensor.nova_pm10
```

- [x] **Step 2: Imports**

Append to `sensor:`:

```yaml
  - platform: homeassistant
    id: thermo1_current
    entity_id: ${thermo1_entity}
    attribute: current_temperature
    on_value:
      - lvgl.label.update: { id: thermo1_value, text: { format: "%.1f °C", args: [x] } }
  - platform: homeassistant
    id: thermo1_target
    entity_id: ${thermo1_entity}
    attribute: temperature
    on_value:
      - lvgl.label.update: { id: thermo1_target_lbl, text: { format: "set %.1f", args: [x] } }
  - platform: homeassistant
    id: thermo2_current
    entity_id: ${thermo2_entity}
    attribute: current_temperature
    on_value:
      - lvgl.label.update: { id: thermo2_value, text: { format: "%.1f °C", args: [x] } }
  - platform: homeassistant
    id: thermo2_target
    entity_id: ${thermo2_entity}
    attribute: temperature
    on_value:
      - lvgl.label.update: { id: thermo2_target_lbl, text: { format: "set %.1f", args: [x] } }
  - platform: homeassistant
    id: pm25
    entity_id: ${pm25_entity}
    on_value:
      - lvgl.label.update: { id: pm25_value, text: { format: "%.0f", args: [x] } }
      - if:
          condition: { lambda: 'return x < 10;' }
          then: [ lvgl.label.update: { id: pm25_value, text_color: 0x40C040 } ]
          else:
            - if:
                condition: { lambda: 'return x < 20;' }
                then: [ lvgl.label.update: { id: pm25_value, text_color: 0xE0C040 } ]
                else: [ lvgl.label.update: { id: pm25_value, text_color: 0xE04040 } ]
  - platform: homeassistant
    id: pm10
    entity_id: ${pm10_entity}
    on_value:
      - lvgl.label.update: { id: pm10_value, text: { format: "%.0f", args: [x] } }
      - if:
          condition: { lambda: 'return x < 20;' }
          then: [ lvgl.label.update: { id: pm10_value, text_color: 0x40C040 } ]
          else:
            - if:
                condition: { lambda: 'return x < 40;' }
                then: [ lvgl.label.update: { id: pm10_value, text_color: 0xE0C040 } ]
                else: [ lvgl.label.update: { id: pm10_value, text_color: 0xE04040 } ]
```

Add a new top-level block:

```yaml
text_sensor:
  - platform: homeassistant
    id: thermo1_mode
    entity_id: ${thermo1_entity}
    on_value:
      - lvgl.label.update: { id: thermo1_mode_lbl, text: !lambda 'return x;' }
  - platform: homeassistant
    id: thermo2_mode
    entity_id: ${thermo2_entity}
    on_value:
      - lvgl.label.update: { id: thermo2_mode_lbl, text: !lambda 'return x;' }
```

- [x] **Step 3: Clock and link state**

Extend the existing blocks:

```yaml
time:
  - platform: homeassistant
    id: ha_time
    on_time:
      - seconds: 0
        then:
          - lvgl.label.update:
              id: lbl_time
              text: !lambda 'return id(ha_time).now().strftime("%H:%M");'

api:
  encryption:
    key: !secret api_encryption_key
  on_client_connected:
    - lvgl.label.update: { id: lbl_link, text: "HA", text_color: 0x40C040 }
  on_client_disconnected:
    - lvgl.label.update: { id: lbl_link, text: "HA", text_color: 0xE04040 }
```

- [x] **Step 4: Styles, header, navigation and the pages**

Replace the whole `pages:` list under `lvgl:` (and add `style_definitions` / `top_layer` beside it):

```yaml
  style_definitions:
    - id: tile_style
      bg_color: 0x1E2430
      bg_opa: COVER
      radius: 10
      border_width: 0
      pad_all: 6
    - id: row_style
      bg_color: 0x1E2430
      bg_opa: COVER
      radius: 8
      border_width: 0
      pad_all: 4
    - id: nav_style
      bg_color: 0x2A3242
      radius: 8
  top_layer:
    widgets:
      - label: { id: lbl_time, x: 8, y: 8, text: "--:--", text_color: 0xFFFFFF }
      - label: { id: lbl_title, align: TOP_MID, y: 8, text: "Home", text_color: 0xA0A8B8 }
      - label: { id: lbl_link, x: 208, y: 8, text: "HA", text_color: 0xE04040 }
      - button:
          x: 8
          y: 286
          width: 60
          height: 28
          styles: nav_style
          widgets: [ label: { align: CENTER, text: "<" } ]
          on_click: [ lvgl.page.previous ]
      - button:
          x: 172
          y: 286
          width: 60
          height: 28
          styles: nav_style
          widgets: [ label: { align: CENTER, text: ">" } ]
          on_click: [ lvgl.page.next ]
  pages:
    - id: page_home
      bg_color: 0x000000
      on_load: [ lvgl.label.update: { id: lbl_title, text: "Home" } ]
      widgets:
        - obj:
            x: 8
            y: 36
            width: 108
            height: 118
            styles: tile_style
            on_click:                           # tap the tile: heat -> off -> auto -> heat
              - homeassistant.action:
                  action: climate.set_hvac_mode
                  data:
                    entity_id: "${thermo1_entity}"
                    hvac_mode: !lambda |-
                      std::string m = id(thermo1_mode).state;
                      if (m == "heat") return std::string("off");
                      if (m == "off") return std::string("auto");
                      return std::string("heat");
            widgets:
              - label: { text: "${thermo1_name}", text_font: montserrat_14, text_color: 0xA0A8B8, align: TOP_LEFT }
              - label: { id: thermo1_value, text: "--", text_font: montserrat_28, text_color: 0xFFFFFF, align: CENTER }
              - label: { id: thermo1_target_lbl, text: "set --", text_font: montserrat_14, text_color: 0xA0A8B8, align: BOTTOM_LEFT }
              - label: { id: thermo1_mode_lbl, text: "--", text_font: montserrat_14, text_color: 0xE0C040, align: BOTTOM_RIGHT }
        - obj:
            x: 124
            y: 36
            width: 108
            height: 118
            styles: tile_style
            on_click:                           # tap the tile: heat -> off -> auto -> heat
              - homeassistant.action:
                  action: climate.set_hvac_mode
                  data:
                    entity_id: "${thermo2_entity}"
                    hvac_mode: !lambda |-
                      std::string m = id(thermo2_mode).state;
                      if (m == "heat") return std::string("off");
                      if (m == "off") return std::string("auto");
                      return std::string("heat");
            widgets:
              - label: { text: "${thermo2_name}", text_font: montserrat_14, text_color: 0xA0A8B8, align: TOP_LEFT }
              - label: { id: thermo2_value, text: "--", text_font: montserrat_28, text_color: 0xFFFFFF, align: CENTER }
              - label: { id: thermo2_target_lbl, text: "set --", text_font: montserrat_14, text_color: 0xA0A8B8, align: BOTTOM_LEFT }
              - label: { id: thermo2_mode_lbl, text: "--", text_font: montserrat_14, text_color: 0xE0C040, align: BOTTOM_RIGHT }
        - obj:
            x: 8
            y: 162
            width: 108
            height: 118
            styles: tile_style
            widgets:
              - label: { text: "PM2.5", text_font: montserrat_14, text_color: 0xA0A8B8, align: TOP_LEFT }
              - label: { id: pm25_value, text: "--", text_font: montserrat_28, text_color: 0xFFFFFF, align: CENTER }
              - label: { text: "ug/m3  ok<10", text_font: montserrat_14, text_color: 0xA0A8B8, align: BOTTOM_LEFT }
        - obj:
            x: 124
            y: 162
            width: 108
            height: 118
            styles: tile_style
            widgets:
              - label: { text: "PM10", text_font: montserrat_14, text_color: 0xA0A8B8, align: TOP_LEFT }
              - label: { id: pm10_value, text: "--", text_font: montserrat_28, text_color: 0xFFFFFF, align: CENTER }
              - label: { text: "ug/m3  ok<20", text_font: montserrat_14, text_color: 0xA0A8B8, align: BOTTOM_LEFT }
    - id: page_lights
      bg_color: 0x000000
      on_load: [ lvgl.label.update: { id: lbl_title, text: "Lights" } ]
      widgets: [ label: { align: CENTER, text: "Lights (Task 6)", text_color: 0xFFFFFF } ]
    - id: page_server
      bg_color: 0x000000
      on_load: [ lvgl.label.update: { id: lbl_title, text: "Server" } ]
      widgets: [ label: { align: CENTER, text: "Server (Task 7)", text_color: 0xFFFFFF } ]
    - id: page_vms
      bg_color: 0x000000
      on_load: [ lvgl.label.update: { id: lbl_title, text: "VMs" } ]
      widgets: [ label: { align: CENTER, text: "VMs (Task 7)", text_color: 0xFFFFFF } ]
    - id: page_storage
      bg_color: 0x000000
      on_load: [ lvgl.label.update: { id: lbl_title, text: "Storage" } ]
      widgets: [ label: { align: CENTER, text: "Storage (Task 8)", text_color: 0xFFFFFF } ]
```

- [x] **Step 5: Validate, compile, flash; checkpoint (user)**

```bash
esphome run waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
```

Expected: header with clock (within a minute), "Home", green `HA`; two thermostat tiles showing current temperature, `set NN.N` and the mode word, and tapping a tile cycles its mode heat → off → auto (visible in HA within a second); PM tiles coloured by threshold; `<`/`>` cycle through the five pages and the title follows.

Result 2026-09-03: passed — values live, HA green, mode cycles from a tap, no overflow. Thermostat names set to "Theo's room" / "Living Room". Build: 1.23 MB flash (15 %), 113 KB RAM (33 %); one-off `lvgl took a long time (110 ms)` at first render.

- [x] **Step 6: Commit**

```bash
git add waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
git commit -m "waveshare: header, page navigation, Home page with thermostats and PM tiles"
```

---

### Task 6: Lights page — four lamps, each with an on/off switch and a 0–255 brightness slider

Revised 2026-09-03 after the first version was on the panel: the user wants only the three lamps with toggle + brightness. Brightness is driven on the light entity itself (`light.turn_on` with `brightness: 0–255`) and read back from the light's `brightness` attribute, so it does not depend on the `input_number` helpers' ranges.

**Files:**
- Modify: `esphome/lcd2.yaml` — `substitutions:`, `binary_sensor:` + `sensor:` imports, `page_lights` widgets

**Interfaces:**
- Consumes: `tile_style`, HA setting "Allow the device to perform Home Assistant actions".
- Produces: switch ids `sw_philips`, `sw_tripod`, `sw_innr`, `sw_bedside`; slider ids `sl_philips`, `sl_tripod`, `sl_innr`, `sl_bedside`.

- [x] **Step 1: Substitutions**

```yaml
  # --- Lights page --- (brightness is driven on the light itself, 0-255, not via helpers)
  philips_entity: light.philips_lamp
  philips_name: "Philips lamp"
  tripod_entity: light.extended_color_light_10
  tripod_name: "Tripod lamp"
  innr_entity: light.innr1
  innr_name: "Bedroom Lamp"
  bedside_entity: light.extended_color_light_1
  bedside_name: "Bedside Lamp"
```

- [x] **Step 2: State feedback**

One `binary_sensor` (on/off → switch) and one `sensor` (brightness attribute → slider) per lamp; the Philips pair is shown, the other two are identical with `tripod` / `innr`:

```yaml
binary_sensor:
  - platform: homeassistant
    id: philips_state
    entity_id: ${philips_entity}
    on_state:
      - lvgl.widget.update:
          id: sw_philips
          state:
            checked: !lambda 'return x;'
```

```yaml
  - platform: homeassistant
    id: philips_bright
    entity_id: ${philips_entity}
    attribute: brightness             # 0-255; absent while the light is off (no update then)
    on_value:
      - lvgl.slider.update: { id: sl_philips, value: !lambda 'return x;' }
```

- [x] **Step 3: The page**

Four cards of 58 px at y = 38, 100, 162, 224 (name top-left, switch top-right, slider along the bottom). One card shown; the others differ only in ids and substitutions. (Bedside lamp added 2026-09-03; Innr renamed "Bedroom Lamp".)

```yaml
    - id: page_lights
      bg_color: 0x000000
      on_load: [ lvgl.label.update: { id: lbl_title, text: "Lights" } ]
      widgets:
        - obj:
            x: 8
            y: 38
            width: 224
            height: 58
            styles: tile_style
            widgets:
              - label: { text: "${philips_name}", text_color: 0xFFFFFF, align: TOP_LEFT }
              - switch:
                  id: sw_philips
                  align: TOP_RIGHT
                  width: 50
                  height: 24
                  on_value:
                    - if:
                        condition:
                          lambda: 'return x;'
                        then:
                          - homeassistant.action:
                              action: light.turn_on
                              data:
                                entity_id: "${philips_entity}"
                        else:
                          - homeassistant.action:
                              action: light.turn_off
                              data:
                                entity_id: "${philips_entity}"
              - slider:
                  id: sl_philips
                  align: BOTTOM_MID
                  y: -3
                  width: 200
                  height: 8
                  min_value: 0
                  max_value: 255
                  on_release:
                    - homeassistant.action:
                        action: light.turn_on
                        data:
                          entity_id: "${philips_entity}"
                          brightness: !lambda 'return to_string((int) x);'
```

- [x] **Step 4: Validate, compile, flash; checkpoint (user)**

Expected: each switch shows the current state at boot and follows changes made in HA; flipping toggles the lamp within a second; dragging a slider and releasing sets the lamp's brightness (the HA light card shows the new level); the slider follows brightness changes made in HA. A slider does not move while its lamp is off (HA reports no brightness then).

First version (six rows incl. candle, timer and daylight toggles and helper-driven sliders) was flashed and worked, then replaced at the user's request. Four-lamp version confirmed on 2026-09-03: states, toggles and 0–255 sliders all working, nothing clipped.

- [x] **Step 5: Commit**

```bash
git add waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
git commit -m "waveshare: Lights page with four lamps, on/off and 0-255 brightness"
```

---

### Task 7: Server and VMs pages — bar rows with severity colours

**Files:**
- Modify: `esphome/lcd2.yaml` — `substitutions:`, `sensor:` imports, `page_server` and `page_vms` widgets

**Interfaces:**
- Consumes: `row_style`.
- Produces: bar ids `bar_cpu_temp`, `bar_gpu_temp`, `bar_nvme_temp`, `bar_px_cpu`, `bar_px_ram`, `bar_px_disk`, `bar_vm_cpu`, `bar_vm_ram`, `bar_vm_disk`, `bar_vm_io`, `bar_ha_cpu`, `bar_ha_ram`, `bar_ha_disk`; matching `val_*` labels.

**The bar-row pattern** (one metric = one widget row + one import). Thresholds come from the user's HA gauges (yellow / red):

```yaml
        - obj:                                  # widget row: name left, value right, bar below
            x: 8
            y: 38
            width: 224
            height: 38
            styles: row_style
            widgets:
              - label: { text: "CPU (Tctl)", text_font: montserrat_14, text_color: 0xFFFFFF, align: TOP_LEFT }
              - label: { id: val_cpu_temp, text: "--", text_font: montserrat_14, text_color: 0xFFFFFF, align: TOP_RIGHT }
              - bar:
                  id: bar_cpu_temp
                  align: BOTTOM_MID
                  width: 212
                  height: 8
                  min_value: 0
                  max_value: 100
                  value: 0
                  bg_color: 0x2A3242
                  indicator: { bg_color: 0x3A7BD5 }
```

```yaml
  - platform: homeassistant                     # import: value -> bar + label, colour by severity
    id: cpu_temp
    entity_id: ${cpu_temp_entity}
    on_value:
      - lvgl.bar.update: { id: bar_cpu_temp, value: !lambda 'return x;' }
      - lvgl.label.update: { id: val_cpu_temp, text: { format: "%.0f °C", args: [x] } }
      - if:
          condition: { lambda: 'return x < 70;' }
          then: [ lvgl.label.update: { id: val_cpu_temp, text_color: 0x40C040 } ]
          else:
            - if:
                condition: { lambda: 'return x < 85;' }
                then: [ lvgl.label.update: { id: val_cpu_temp, text_color: 0xE0C040 } ]
                else: [ lvgl.label.update: { id: val_cpu_temp, text_color: 0xE04040 } ]
```

- [x] **Step 1: Substitutions**

```yaml
  # --- Server page (Proxmox host) ---
  cpu_temp_entity: sensor.cpu_temp
  gpu_temp_entity: sensor.gpu_temp
  nvme_temp_entity: sensor.nvme_temp
  px_cpu_entity: sensor.proxmox_cpu_usage
  px_ram_entity: sensor.proxmox_ram_usage
  px_disk_entity: sensor.proxmox_root_disk_usage
  # --- VMs page ---
  vm_cpu_entity: sensor.cpu_usage
  vm_ram_entity: sensor.ram_usage
  vm_disk_entity: sensor.root_disk_usage
  vm_io_entity: sensor.cpu_iowait
  ha_cpu_entity: sensor.processor_use
  ha_ram_entity: sensor.memory_use_percent
  ha_disk_entity: sensor.disk_use_percent
```

- [x] **Step 2: Server page**

Six rows (y = 38, 79, 120, 161, 202, 243), using the pattern above with: `CPU (Tctl)` 0–100 °C yellow 70 red 85; `GPU (edge)` 0–100 °C 70/85; `NVMe` 0–94 °C 60/75; `Proxmox CPU` % 70/90; `Proxmox RAM` % 90/95; `Proxmox disk` % 70/85. Value formats `%.0f °C` for temperatures, `%.0f %%` for percentages. Six imports with the matching thresholds.

- [x] **Step 3: VMs page**

Seven rows, 32 px tall at y = 38 + 35·k (k = 0..6): `Ubuntu CPU` 70/90, `Ubuntu RAM` 90/95, `Ubuntu disk` 70/85, `Ubuntu IO wait` 10/20, `HA CPU` 70/90, `HA RAM` 90/95, `HA disk` 70/85, all `%.0f %%`. For 32 px rows use `height: 32`, bar `height: 6`.

- [x] **Step 4: Validate, compile, flash; checkpoint (user)**

Expected: both pages show live values matching the HA gauges; a value crossing its yellow threshold turns the number yellow (e.g. start a CPU-heavy job on the VM). No layout overlap: each row's name, value and bar visible.

Result 2026-09-03: passed — values match the HA gauges, no overlap in the 32 px VMs rows. Build 1.26 MB flash / 120 KB RAM.

- [x] **Step 5: Commit**

```bash
git add waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
git commit -m "waveshare: Server and VMs pages with severity-coloured bar rows"
```

---

### Task 8: Storage page — disk usage, temperatures, NVMe wear, RAID, SMART

**Files:**
- Modify: `esphome/lcd2.yaml` — `substitutions:`, `sensor:`/`text_sensor:` imports, `page_storage` widgets

**Interfaces:**
- Consumes: bar-row pattern (Task 7), `row_style`.
- Produces: `bar_bigdata_use`, `bar_jelly_use`, `bar_nvme_wear`, `val_bigdata_temp`, `val_jelly_temp`, `lbl_raid`, `lbl_smart`.

- [x] **Step 1: Substitutions**

```yaml
  # --- Storage page ---
  bigdata_use_entity: sensor.bigdata_disk_usage
  jelly_use_entity: sensor.jellymedia_disk_usage
  nvme_wear_entity: sensor.nvme_percentage_used
  bigdata_temp_entity: sensor.bigdata_temp
  jelly_temp_entity: sensor.jellymedia_temp
  raid_active_entity: sensor.raid_active_disks
  raid_failed_entity: sensor.raid_failed_disks
  smart_health_entity: sensor.smart_overall_health
```

- [x] **Step 2: Widgets and imports**

Rows (y = 38, 73, 108 with height 32): bar rows `BigData use` % 70/85, `JellyMedia use` % 70/85, `NVMe wear` % 50/80. Row y = 143 (height 32): two temperature values side by side, `BigData NN °C` (45/55) and `Jelly NN °C` (45/55), coloured by severity, no bar. Row y = 178 (height 44): label `lbl_raid` text `RAID md0: A active, F failed` — imports of `raid_active_entity` and `raid_failed_entity` each update the label with a lambda combining `id(raid_active).state` and `id(raid_failed).state`; red when failed > 0, green otherwise. Row y = 226 (height 44): label `lbl_smart` fed by a `text_sensor` import of `${smart_health_entity}`; text as delivered by HA.

```yaml
text_sensor:
  - platform: homeassistant
    id: smart_health
    entity_id: ${smart_health_entity}
    on_value: [ lvgl.label.update: { id: lbl_smart, text: !lambda 'return "SMART: " + x;' } ]
```

```yaml
  - platform: homeassistant
    id: raid_active
    entity_id: ${raid_active_entity}
    on_value: [ script.execute: update_raid ]
  - platform: homeassistant
    id: raid_failed
    entity_id: ${raid_failed_entity}
    on_value: [ script.execute: update_raid ]
```

```yaml
script:
  - id: update_raid
    then:
      - lvgl.label.update:
          id: lbl_raid
          text: !lambda 'char b[40]; snprintf(b, sizeof(b), "RAID md0: %.0f active, %.0f failed", id(raid_active).state, id(raid_failed).state); return std::string(b);'
      - if:
          condition: { lambda: 'return id(raid_failed).state > 0;' }
          then: [ lvgl.label.update: { id: lbl_raid, text_color: 0xE04040 } ]
          else: [ lvgl.label.update: { id: lbl_raid, text_color: 0x40C040 } ]
```

- [x] **Step 3: Validate, compile, flash; checkpoint (user)**

Expected: three usage bars, two temperatures, the RAID line in green with the right disk counts, the SMART line with HA's text.

Result 2026-09-03: passed — values match HA, RAID line green with the real counts.

- [x] **Step 4: Commit**

```bash
git add waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
git commit -m "waveshare: Storage page with disk usage, temperatures, NVMe wear, RAID and SMART"
```

---

### Task 9: Idle dimming, touch wake, battery voltage

**Files:**
- Modify: `esphome/lcd2.yaml` — `lvgl.on_idle`, touchscreen `on_touch`, `sensor:` battery

**Interfaces:**
- Consumes: `backlight`, `touch`, `lvgl`.
- Produces: HA sensor `Battery Voltage`; idle behaviour.

- [x] **Step 1: Idle and wake**

Under `lvgl:` add:

```yaml
  on_idle:
    - timeout: 60s
      then:
        - light.turn_off: backlight
        - lvgl.pause:
```

Replace the touchscreen `on_touch` logging with:

```yaml
    on_touch:
      - if:
          condition: lvgl.is_paused
          then:
            - lvgl.resume:
            - lvgl.widget.redraw:
            - light.turn_on:
                id: backlight
                brightness: 100%
```

- [x] **Step 2: Battery voltage**

Append to `sensor:`:

```yaml
  - platform: adc
    pin: GPIO5
    name: "Battery Voltage"
    id: battery_v
    attenuation: 12db
    update_interval: 60s
    filters:
      - multiply: 3.0        # Waveshare demo: V = 3.3/4096 * adc * 3 (1:3 divider)
      - sliding_window_moving_average:
          window_size: 5
          send_every: 1
    unit_of_measurement: "V"
    accuracy_decimals: 2
```

- [x] **Step 3: Validate, compile, flash; checkpoint (user)**

Expected: after 60 s untouched the screen goes dark; one touch brings it back with the UI intact (no ghosting); `Battery Voltage` in HA reads ~4.9–5.1 V on USB without a battery, or the cell voltage (3.3–4.2 V) with one; compare with a multimeter on the MX1.25 pads and adjust `multiply` if off by more than 0.1 V.

Result 2026-09-03: passed — screen dims after 60 s, one touch restores the page intact, Battery Voltage entity present in HA (built together with Task 11).

- [x] **Step 4: Commit**

```bash
git add waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
git commit -m "waveshare: idle backlight off with touch wake, battery voltage sensor"
```

---

### Task 10: Documentation and finish

**Files:**
- Modify: `waveshare/esp32-s3-touch-lcd-2/README.md`
- Create: `waveshare/esp32-s3-touch-lcd-2/docs/page-rooms.jpg`, `page-home.jpg`, `page-lights.jpg`, `page-server.jpg`, `page-vms.jpg`, `page-storage.jpg` (user photos, EXIF-stripped)
- Modify: `README.md` at the repo root (one line listing the project)

- [x] **Step 1: README**

Replace the "Status: planned" paragraph with: what it does, the six pages with photos, HA setup (ESPHome integration, API key, "Allow the device to perform Home Assistant actions"), build/flash commands (venv path, `esphome run`, first-flash download mode), how to add a tile or a switch (the three places), the idle behaviour, and a troubleshooting list (colour order, invert, touch transform, skip_probe). Keep the board pin table.

- [x] **Step 2: Repo README**

Add a bullet for `waveshare/esp32-s3-touch-lcd-2` next to the existing projects.

- [x] **Step 3: Commit and finish**

```bash
git add waveshare/esp32-s3-touch-lcd-2 README.md
git commit -m "waveshare docs: ESP32-S3-Touch-LCD-2 dashboard README, photos, repo index"
```

Then use superpowers:finishing-a-development-branch (merge / PR / keep).

Result 2026-09-03: README written (pages, HA setup, build/flash, extending, troubleshooting); repo index row added. Page photos not yet taken — the six `<img>` tags in the README point at `docs/page-*.jpg` to be added.

---

### Task 11: Rooms page — temperature and humidity per room, shown first

Added 2026-09-03 at the user's request (built together with Task 9, before Task 10). Four rooms, each a tile with the temperature large and the humidity below; this page is first in `pages:` so it is the home screen; the thermostat/PM page follows.

**Files:**
- Modify: `esphome/lcd2.yaml` — `substitutions:`, eight `sensor:` imports, `page_rooms` inserted before `page_home`, header default title "Rooms"

**Interfaces:**
- Consumes: `tile_style`.
- Produces: label ids `room1_temp_lbl`..`room4_temp_lbl`, `room1_hum_lbl`..`room4_hum_lbl`; page `page_rooms`.

- [x] **Step 1: Substitutions**

```yaml
  # --- Rooms page (first page): temperature + humidity per room ---
  room1_name: "Bedroom"
  room1_temp_entity: sensor.multi_sensor_bedzimmer
  room1_hum_entity: sensor.multi_sensor_bedzimmer_2
  room2_name: "Theo's room"
  room2_temp_entity: sensor.multi_sensor_wohnzimmer
  room2_hum_entity: sensor.multi_sensor_wohnzimmer_2
  room3_name: "Bathroom"
  room3_temp_entity: sensor.temperature_9
  room3_hum_entity: sensor.humidity_10
  room4_name: "Living Room"
  room4_temp_entity: sensor.living_room
  room4_hum_entity: sensor.living_room_2
```

- [x] **Step 2: Imports and tiles**

Per room, two imports (`%.1f °C` → `roomN_temp_lbl`, `%.1f %%` → `roomN_hum_lbl`) and one tile at (8|124, 36|162), 108×118: name top-left (grey, 14), temperature centre (white, 28), humidity bottom-left (blue, 14) with "RH" bottom-right. Same tile geometry as the Home page.

- [x] **Step 3: Validate, compile, flash; checkpoint (user)**

Expected: the panel boots on "Rooms" with the four tiles matching the HA Temperature / Humidity card; `>` leads to Home (thermostats + PM), then Lights, Server, VMs, Storage.

Result 2026-09-03: passed — boots on Rooms, values match the HA card, page order as designed. Build 1.29 MB flash / 126 KB RAM.

- [x] **Step 4: Commit**

```bash
git add waveshare/esp32-s3-touch-lcd-2/esphome/lcd2.yaml
git commit -m "waveshare: Rooms page with temperature and humidity tiles as the first page"
```

---

## Later ideas (not tasks)

- Material Design Icons font (`font: - file: gfonts://...` or a local `.ttf`) for tile icons and the header.
- QMI8658 IMU via an external component for auto-rotate or a tap-to-wake.
- TF card via the `sd_mmc_card` external component (shares SPI CS 41) for logging without HA.
- Camera: `esp32_camera` with the DVP pins in §2 of the spec, streaming to HA.

## Self-review notes

- Spec §1 scope → Tasks 2–9 (pages revised to the user's entities on 2026-09-03: Home, Lights, Server, VMs, Storage); §2 pins → Global Constraints + Tasks 3/4/7; §3 architecture → Tasks 2, 5, 6; §4 UI → Tasks 5–7; §5 substitutions → Tasks 5, 6; §6 HA-side → Task 2 checkpoint, Task 6 checkpoint; §7 verification → each checkpoint; §8 fallbacks → Task 3/4 checkpoints.
- Names: `lcd`, `touch`, `backlight`, `ha_time` (2–3) used in 4–7; `page_home/page_controls`, `tile1_value..tile4_value`, `lbl_time`, `lbl_link` (5) used in 6–7; `sw1..sw4` and `sw1_state..` (6). The `mipi_spi` model name and the `homeassistant.action` `action:` key are current-release syntax; if `esphome config` rejects either, the legacy `st7789v` snippet (Task 3) and `service:` (older releases) are the fallbacks.
