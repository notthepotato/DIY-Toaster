// ============================================================================
//  power.h  -  fuel gauge, alarms, energy bookkeeping
// ============================================================================
#pragma once
#include <Arduino.h>
#include "pdp_math.h"

enum Alarm : uint16_t {
  ALM_NONE       = 0,
  ALM_NO_BATTERY = 1 << 0,  // VIN < 3 V: running from USB only (informational)
  ALM_BAT_LOW    = 1 << 1,
  ALM_BAT_CRIT   = 1 << 2,
  ALM_BAT_HIGH   = 1 << 3,  // above the chemistry's max: wrong battery setting or charger issue
  ALM_VIN_DROP   = 1 << 4,  // VIN < 6 V: the 5 V buck is close to dropout
  ALM_IN_OVER    = 1 << 5,
  ALM_OUT_OVER   = 1 << 6,
  ALM_IN_LIMIT   = 1 << 7,  // battery-side sensor pin near the ADC limit
  ALM_OUT_LIMIT  = 1 << 8,  // 5 V-rail sensor pin near the ADC limit (see README)
  ALM_TEMP       = 1 << 9,
};
constexpr int ALARM_COUNT = 10;

namespace power {
void begin();
void update();                     // call after each sensors::update() block
const pdpmath::Gauge &gauge();
float socPct();                    // -1 until initialised / no battery
int32_t runtimeMin();              // -1 unknown
float cellVolts();                 // loaded, per cell
uint16_t alarms();
bool critical();                   // anything that should make the LEDs shout
const char *alarmName(int bit);
String alarmList(uint16_t mask);
String runtimeStr();               // "3h12m", "--"
void setSoc(float pct);
void reseed();                     // forget the gauge, re-estimate from voltage
void resetSession();
void saveSoc();
// called when an alarm turns on/off (for logging)
typedef void (*AlarmCb)(uint16_t bit, bool on);
void onAlarm(AlarmCb cb);
}  // namespace power
