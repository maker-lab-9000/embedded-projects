// Structural config (committed). The entity list itself is generated from .env
// into generated_entities.h (gitignored). Edit .env + rerun tools/gen_config.py.
#pragma once

// ENT_ prefix avoids colliding with CONTROL/DISPLAY macros in the ESP32/M5 headers.
enum EntityRole { ENT_CONTROL, ENT_DISPLAY };

struct EntityCfg {
  const char* entity_id;
  const char* label;
  EntityRole  role;
  const char* unit;
};

#include "generated_entities.h"
static const int ENTITY_COUNT = sizeof(ENTITIES) / sizeof(ENTITIES[0]);
