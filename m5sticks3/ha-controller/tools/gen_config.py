#!/usr/bin/env python3
"""Generate secrets.h / generated_entities.h from .env (gitignored)."""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
ENV = os.path.join(PROJ, ".env")
M1 = os.path.join(PROJ, "firmware", "m1_link")
M2 = os.path.join(PROJ, "firmware", "m2_controller")

def die(msg):
    print("gen_config: " + msg, file=sys.stderr)
    sys.exit(1)

if not os.path.exists(ENV):
    die(".env not found. Copy .env.example to .env and fill it in.")

env = {}
with open(ENV) as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        env[k.strip()] = v.strip()

REQUIRED = ["WIFI_SSID", "WIFI_PASSWORD", "HA_HOST", "HA_PORT", "HA_TOKEN",
            "LIGHT_ENTITY", "LIGHT_LABEL"]
missing = [k for k in REQUIRED if not env.get(k)]
if missing:
    die("missing required keys in .env: " + ", ".join(missing))

def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

secrets = "\n".join([
    "// AUTO-GENERATED from .env by tools/gen_config.py - do not edit, gitignored.",
    "#pragma once",
    "#define WIFI_SSID " + cstr(env["WIFI_SSID"]),
    "#define WIFI_PASSWORD " + cstr(env["WIFI_PASSWORD"]),
    "#define HA_HOST " + cstr(env["HA_HOST"]),
    "#define HA_PORT " + env["HA_PORT"],
    "#define HA_TOKEN " + cstr(env["HA_TOKEN"]),
    "#define HA_TEST_ENTITY " + cstr(env["LIGHT_ENTITY"]),  # for m1_link
    "",
]) + "\n"

# entities: control light first, then DISPLAY_ENTITIES
rows = ['  {%s, %s, CONTROL, ""},' % (cstr(env["LIGHT_ENTITY"]), cstr(env["LIGHT_LABEL"]))]
for item in [x for x in env.get("DISPLAY_ENTITIES", "").split(";") if x.strip()]:
    parts = item.split("|")
    if len(parts) != 3:
        die("bad DISPLAY_ENTITIES item (need id|label|unit): " + item)
    eid, label, unit = (p.strip() for p in parts)
    rows.append('  {%s, %s, DISPLAY, %s},' % (cstr(eid), cstr(label), cstr(unit)))

entities = "\n".join([
    "// AUTO-GENERATED from .env by tools/gen_config.py - do not edit, gitignored.",
    "#pragma once",
    "static const EntityCfg ENTITIES[] = {",
    "\n".join(rows),
    "};",
    "",
]) + "\n"

for d in (M1, M2):
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "secrets.h"), "w") as f:
        f.write(secrets)
with open(os.path.join(M2, "generated_entities.h"), "w") as f:
    f.write(entities)

print("gen_config: wrote secrets.h (m1_link, m2_controller) and "
      "generated_entities.h (%d entities)" % (len(rows)))
