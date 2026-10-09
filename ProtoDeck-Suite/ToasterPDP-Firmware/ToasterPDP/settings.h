// ============================================================================
//  settings.h  -  everything the console can change, persisted in NVS
// ============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "src/protolink/protolink.h"

enum FanMode : uint8_t { FAN_MANUAL = 0, FAN_AUTO = 1 };

struct Settings {
  uint16_t magic;
  char     name[20];

  // battery
  uint8_t  chem;          // 0 Li-ion, 1 LiFePO4, 2 lead-acid
  uint8_t  cells;
  uint32_t capacityMah;
  uint16_t rintMilliOhm;

  // calibration
  float    vinGain;       // multiplies VIN_DIVIDER result
  float    inZeroMv, outZeroMv;     // ACS724 output at 0 A, as seen at the pin
  float    inGain, outGain;         // multiplies the nominal 400 mV/A slope
  float    inFrontend, outFrontend; // divider ratio between sensor and XIAO (1.0 = direct)

  // limits
  float    limInA, limOutA, limTempC;

  // fan
  uint8_t  fanMode;
  uint8_t  fanDuty;       // manual duty, percent
  uint8_t  fanMin;        // auto mode: duty at/below fanT0, percent
  uint8_t  fanT0, fanT1;  // auto mode: temperature ramp start/end (C)

  // LEDs
  uint16_t ledCount;      // external pixels on P1
  uint8_t  statusBright;
  PlLedFx  strip;         // effect for the external pixels (PL_FX_ANIM = battery gauge)

  // links
  uint8_t  channel;
  uint16_t streamMs;      // JSON lines on the SERIAL header, 0 = off
};

namespace settings {
Settings &get();
void load();
void save();
void defaults();
}  // namespace settings

// Persisted fuel-gauge state (written every few minutes, not on every change).
namespace socstore {
bool load(float &socPct, float &atVolts);
void save(float socPct, float atVolts);
}  // namespace socstore
