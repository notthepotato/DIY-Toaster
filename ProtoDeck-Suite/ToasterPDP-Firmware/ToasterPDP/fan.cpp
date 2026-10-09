#include "fan.h"
#include "config.h"
#include "pdp_math.h"
#include "sensors.h"
#include "settings.h"
#include <esp_private/gpio.h>  // gpio_od_enable(): open-drain without detaching LEDC

namespace fan {
namespace {

uint8_t out = 0;
volatile uint32_t pulses = 0;
uint32_t lastCalc = 0, lastRpm = 0, lastPulses = 0;
int32_t rpmVal = -1;
bool pwmOk = false;

void IRAM_ATTR onTach() { pulses = pulses + 1; }

void write(uint8_t pct) {
  out = pct > 100 ? 100 : pct;
  if (pwmOk) ledcWrite(PIN_FAN_PWM, (uint32_t)out * 255 / 100);
}

}  // namespace

bool hasTach() { return (int)PIN_FAN_TACH >= 0; }
uint8_t dutyPct() { return out; }
int32_t rpm() { return rpmVal; }

void begin() {
  pwmOk = ledcAttach(PIN_FAN_PWM, FAN_PWM_HZ, 8);
#if FAN_OPEN_DRAIN
  if (pwmOk) gpio_od_enable((gpio_num_t)PIN_FAN_PWM);  // fan's own pull-up makes the "high"
#endif
  if (hasTach()) {
    pinMode(PIN_FAN_TACH, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FAN_TACH), onTach, FALLING);
  }
  write(settings::get().fanDuty);
}

void update() {
  uint32_t now = millis();
  if (now - lastCalc < 250) return;
  lastCalc = now;
  const Settings &s = settings::get();
  const Readings &r = sensors::get();
  if (s.fanMode == FAN_AUTO) {
    float t = isnan(r.tempC) ? 0 : r.tempC;
    write(pdpmath::fanAuto(t, r.pout, s.fanMin, s.fanT0, s.fanT1));
  } else {
    write(s.fanDuty);
  }

  if (hasTach() && now - lastRpm >= 1000) {
    uint32_t p = pulses;
    uint32_t dp = p - lastPulses;
    lastPulses = p;
    float secs = (now - lastRpm) / 1000.0f;
    lastRpm = now;
    if (p == 0) rpmVal = -1;  // nothing connected
    else rpmVal = (int32_t)(dp / (float)FAN_TACH_PULSES_REV / secs * 60.0f);
  }
}

}  // namespace fan
