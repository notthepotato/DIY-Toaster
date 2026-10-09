// ============================================================================
//  leds.h  -  onboard status NeoPixel (D2) + optional strip on P1
//   status pixel: battery colour (green -> red), blue breathing on USB power,
//                 fast red/amber blink on alarms, white strobe for "identify",
//                 short cyan flick when the ProtoDeck talks to us
//   strip:        any ProtoLink effect, or ANIM = battery gauge bar
// ============================================================================
#pragma once
#include <Arduino.h>

namespace leds {
void begin();
void update();                 // ~50 fps internally
void setCount(uint16_t n);     // external pixels on P1
void identify(uint32_t ms = 2000);
void flick();                  // link activity
void test();                   // R, G, B, white sweep (blocking ~2 s)
float estimateStripmA();       // what the strip should be drawing right now
}  // namespace leds
