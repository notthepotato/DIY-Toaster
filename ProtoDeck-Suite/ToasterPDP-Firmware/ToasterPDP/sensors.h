// ============================================================================
//  sensors.h  -  battery voltage + both ACS724 current sensors + chip temp
// ============================================================================
#pragma once
#include <Arduino.h>

struct Readings {
  // filtered (~0.25 s) values
  float vin = 0;              // battery / VCC volts
  float iin = 0, iout = 0;    // amps: battery side, 5 V rail
  float pin = 0, pout = 0;    // watts (5 V rail assumes VOUT_NOMINAL)
  float tempC = NAN;          // ESP32-C6 die
  // raw pin voltages (filtered), useful for calibration
  float vinMv = 0, inMv = 0, outMv = 0;
  // session statistics (fast 10 ms blocks, so short spikes are caught)
  float iinPeak = 0, ioutPeak = 0, vinMin = 0, vinMax = 0;
  // health
  bool  inNearLimit = false, outNearLimit = false;  // pin >= ADC_LIMIT_MV in the last second
  bool  batteryPresent = false;                      // VIN above 3 V (else: USB-only)
  float inMaxReadableA = 0, outMaxReadableA = 0;     // current at which the pin reaches the limit
  uint32_t blocks = 0;
};

namespace sensors {
void begin();
bool update();                    // call often; true when a new 10 ms block was processed
const Readings &get();
void resetStats();
// blocking average of the raw pin voltages over `ms` (used by calibration)
void averagePins(uint32_t ms, float &vinMv, float &inMv, float &outMv);
}  // namespace sensors
