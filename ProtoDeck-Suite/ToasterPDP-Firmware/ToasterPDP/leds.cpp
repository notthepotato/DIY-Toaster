#include "leds.h"
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "power.h"
#include "sensors.h"
#include "settings.h"
#include "src/protolink/pl_effects.h"

namespace leds {
namespace {

Adafruit_NeoPixel px(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
uint8_t *frame = nullptr;  // RGB, external pixels only
uint16_t ext = 0;
uint32_t lastFrame = 0, identifyUntil = 0, flickAt = 0;
float stripmA = 0;

uint8_t sc(uint8_t v, uint8_t b) { return plfx::scale8(v, b); }

// green (100 %) -> amber -> red (0 %)
void socColor(float soc, uint8_t *o) {
  float t = constrain(soc, 0.0f, 100.0f) / 100.0f;
  plfx::hsv((uint8_t)(t * 85), 255, 255, o);  // hue 0 = red, 85 = green
}

void renderStatus(uint32_t now, uint8_t *o) {
  const Settings &s = settings::get();
  uint16_t al = power::alarms();
  if (identifyUntil && (int32_t)(identifyUntil - now) > 0) {
    uint8_t v = ((now / 80) & 1) ? 255 : 0;
    o[0] = o[1] = o[2] = v;
    return;
  }
  identifyUntil = 0;
  if (power::critical()) {  // fast red / amber alternation
    bool ph = (now / 150) & 1;
    o[0] = sc(255, s.statusBright * 2 > 255 ? 255 : s.statusBright * 2);
    o[1] = ph ? sc(90, s.statusBright) : 0;
    o[2] = 0;
    return;
  }
  uint8_t breathe = plfx::sin8((uint8_t)(now >> 4));
  uint8_t level = (uint8_t)(s.statusBright * (80 + (uint32_t)breathe * 175 / 255) / 255);
  if (al & ALM_NO_BATTERY) {
    o[0] = 0;
    o[1] = sc(40, level);
    o[2] = sc(255, level);
  } else if (power::socPct() < 0) {
    o[0] = o[1] = o[2] = sc(120, level);  // waiting for the gauge
  } else {
    uint8_t c[3];
    socColor(power::socPct(), c);
    if (al & ALM_BAT_LOW) level = ((now / 500) & 1) ? s.statusBright : s.statusBright / 6;  // slow blink
    o[0] = sc(c[0], level);
    o[1] = sc(c[1], level);
    o[2] = sc(c[2], level);
  }
  if (flickAt && now - flickAt < 60) {
    o[0] = 0;
    o[1] = sc(230, s.statusBright);
    o[2] = sc(255, s.statusBright);
  }
}

// native strip mode: battery fuel gauge bar
void renderGauge(uint32_t now) {
  float soc = power::socPct();
  if (soc < 0) {  // no battery / not ready: dim scanning dot
    PlLedFx f = {PL_ZONE_AUX, PL_FX_SCANNER, 255, 80, 0x10, 0x40, 0xFF, 0, 0, 0};
    plfx::render(f, frame, ext, now);
    return;
  }
  float lit = ext * soc / 100.0f;
  bool crit = power::alarms() & ALM_BAT_CRIT;
  for (uint16_t i = 0; i < ext; i++) {
    uint8_t c[3];
    socColor(100.0f * (i + 0.5f) / ext, c);  // gradient along the bar
    float k = constrain(lit - i, 0.0f, 1.0f);
    if (crit && ((now / 300) & 1)) k = 0;
    uint8_t *p = frame + i * 3;
    p[0] = (uint8_t)(c[0] * k);
    p[1] = (uint8_t)(c[1] * k);
    p[2] = (uint8_t)(c[2] * k);
    if (k == 0) p[0] = p[1] = p[2] = 3;  // faint "empty" segments so the bar length is visible
  }
}

}  // namespace

void setCount(uint16_t n) {
  if (n > LED_MAX_EXTERNAL) n = LED_MAX_EXTERNAL;
  // blank the old length first so pixels past the new end don't stay lit
  px.clear();
  px.show();
  ext = n;
  free(frame);
  frame = ext ? (uint8_t *)calloc(ext, 3) : nullptr;
  if (ext && !frame) ext = 0;
  px.updateLength(1 + ext);
  px.clear();
  px.show();
}

void begin() {
  px.begin();
  setCount(settings::get().ledCount);
}

void identify(uint32_t ms) { identifyUntil = millis() + ms; }
void flick() { flickAt = millis(); }
float estimateStripmA() { return stripmA; }

void test() {
  const uint32_t cols[4] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF};
  for (uint32_t c : cols) {
    for (uint16_t i = 0; i < 1 + ext; i++) px.setPixelColor(i, px.Color((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
    px.setBrightness(40);
    px.show();
    delay(450);
  }
  px.setBrightness(255);
}

void update() {
  uint32_t now = millis();
  if (now - lastFrame < 20) return;
  lastFrame = now;
  const Settings &s = settings::get();

  uint8_t st[3];
  renderStatus(now, st);
  px.setPixelColor(0, st[0], st[1], st[2]);

  if (ext && frame) {
    const PlLedFx &f = s.strip;
    bool ident = identifyUntil && (int32_t)(identifyUntil - now) > 0;
    if (ident) memset(frame, ((now / 80) & 1) ? 255 : 0, ext * 3);
    else if (f.fx == PL_FX_ANIM) renderGauge(now);
    else plfx::render(f, frame, ext, now);
    uint8_t bri = f.brightness;
    for (uint16_t i = 0; i < ext; i++) {
      uint8_t *p = frame + i * 3;
      px.setPixelColor(1 + i, sc(p[0], bri), sc(p[1], bri), sc(p[2], bri));
    }
    stripmA = plfx::estimateCurrentmA(frame, ext, bri);
  } else {
    stripmA = 0;
  }
  px.show();
}

}  // namespace leds
