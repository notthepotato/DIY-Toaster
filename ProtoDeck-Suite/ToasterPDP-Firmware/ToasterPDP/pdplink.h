// ============================================================================
//  pdplink.h  -  ProtoLink (ESP-NOW) node so the ProtoDeck can see and drive the PDP
// ============================================================================
#pragma once
#include <Arduino.h>

namespace pdplink {
void begin();
void loop();
bool started();
bool deckKnown();
uint32_t lastDeckMs();          // millis() of the last packet from a ProtoDeck, 0 = never
void log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
}  // namespace pdplink
