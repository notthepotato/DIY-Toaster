// ============================================================================
//  presets.h  -  LED effect presets stored on the ProtoDeck (NVS / flash)
//  A preset is just a named PlLedFx; it is pushed to a node on "apply".
// ============================================================================
#pragma once
#include <Arduino.h>
#include "protolink.h"

#define PRESET_SLOTS 12

struct Preset {
  char    name[16];
  PlLedFx fx;   // brightness 0 = "leave the node's brightness alone"
};

namespace presets {
void    begin();
int     count();
Preset &get(int i);
void    put(int i, const Preset &p);  // stores + persists
void    resetDefaults();
String  describe(const Preset &p);    // one-line summary
}  // namespace presets

namespace settings {
struct Settings {
  uint8_t backlight = 85;     // %
  uint16_t sleepSec = 120;    // 0 = never
  bool    clicks = false;     // buzzer click on touch (V1.3+)
  bool    skipBoot = false;   // jump straight to the UI
  bool    scanOnBoot = true;
  uint8_t homeChannel = 1;
};
Settings &get();
void load();
void save();
}  // namespace settings
