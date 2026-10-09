// ============================================================================
//  fan.h  -  25 kHz PWM on the FAN header (D9) + optional tach on D8
// ============================================================================
#pragma once
#include <Arduino.h>

namespace fan {
void begin();
void update();          // call every loop; recomputes duty ~4x a second
uint8_t dutyPct();      // what is being output right now
int32_t rpm();          // -1 if no tach pin / no pulses seen yet
bool hasTach();
}  // namespace fan
