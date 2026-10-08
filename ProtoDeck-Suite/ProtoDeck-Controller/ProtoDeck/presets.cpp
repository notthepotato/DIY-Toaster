#include "presets.h"
#include "config.h"
#include <Preferences.h>

namespace {
Preferences prefs;
Preset slots[PRESET_SLOTS];
const uint8_t STORE_VER = 2;

Preset make(const char *name, uint8_t zone, uint8_t fx, uint32_t c1, uint32_t c2, uint8_t speed = 128,
            uint8_t bri = 0) {
  Preset p = {};
  strncpy(p.name, name, sizeof(p.name) - 1);
  p.fx.zone = zone;
  p.fx.fx = fx;
  p.fx.brightness = bri;
  p.fx.speed = speed;
  p.fx.r1 = c1 >> 16; p.fx.g1 = c1 >> 8; p.fx.b1 = c1;
  p.fx.r2 = c2 >> 16; p.fx.g2 = c2 >> 8; p.fx.b2 = c2;
  return p;
}
}  // namespace

namespace presets {

void resetDefaults() {
  slots[0]  = make("CYBER CYAN", PL_ZONE_ALL,  PL_FX_SOLID,    0x22E4FF, 0x000000);
  slots[1]  = make("RAINBOW",    PL_ZONE_ALL,  PL_FX_RAINBOW,  0xFFFFFF, 0x000000, 140);
  slots[2]  = make("PINK PULSE", PL_ZONE_EARS, PL_FX_BREATHE,  0xFF3D8B, 0x1A0510, 110);
  slots[3]  = make("KITT EYES",  PL_ZONE_EARS, PL_FX_SCANNER,  0xFF1020, 0x080000, 150);
  slots[4]  = make("STARFIELD",  PL_ZONE_EARS, PL_FX_SPARKLE,  0xFFFFFF, 0x02040A, 120);
  slots[5]  = make("SUNSET",     PL_ZONE_EARS, PL_FX_GRADIENT, 0xFF6A00, 0xC2185B, 90);
  slots[6]  = make("PLASMA",     PL_ZONE_EARS, PL_FX_PLASMA,   0x22E4FF, 0x7A00FF, 120);
  slots[7]  = make("TOXIC",      PL_ZONE_ALL,  PL_FX_SOLID,    0x3DFF7A, 0x000000);
  slots[8]  = make("ALERT",      PL_ZONE_EARS, PL_FX_STROBE,   0xFFB020, 0x000000, 160);
  slots[9]  = make("COMET",      PL_ZONE_AUX,  PL_FX_CHASE,    0x22E4FF, 0x000814, 140);
  slots[10] = make("NATIVE",     PL_ZONE_ALL,  PL_FX_ANIM,     0x000000, 0x000000);
  slots[11] = make("LIGHTS OUT", PL_ZONE_ALL,  PL_FX_OFF,      0x000000, 0x000000);
  prefs.putBytes("slots", slots, sizeof(slots));
  prefs.putUChar("ver", STORE_VER);
}

void begin() {
  prefs.begin("deckpre", false);
  if (prefs.getUChar("ver", 0) != STORE_VER || prefs.getBytesLength("slots") != sizeof(slots)) resetDefaults();
  else prefs.getBytes("slots", slots, sizeof(slots));
}

int count() { return PRESET_SLOTS; }
Preset &get(int i) { return slots[constrain(i, 0, PRESET_SLOTS - 1)]; }

void put(int i, const Preset &p) {
  if (i < 0 || i >= PRESET_SLOTS) return;
  slots[i] = p;
  slots[i].name[sizeof(slots[i].name) - 1] = 0;
  prefs.putBytes("slots", slots, sizeof(slots));
}

String describe(const Preset &p) {
  char b[96];
  snprintf(b, sizeof(b), "%-12s %-5s %-8s #%02X%02X%02X #%02X%02X%02X spd %3u bri %s", p.name, plZoneName(p.fx.zone),
           plFxName(p.fx.fx), p.fx.r1, p.fx.g1, p.fx.b1, p.fx.r2, p.fx.g2, p.fx.b2, p.fx.speed,
           p.fx.brightness ? String(p.fx.brightness).c_str() : "keep");
  return String(b);
}

}  // namespace presets

namespace settings {
namespace {
Settings s;
Preferences sp;
}
Settings &get() { return s; }
void load() {
  sp.begin("deckcfg", false);
  s.backlight = sp.getUChar("bl", UI_DEFAULT_BACKLIGHT);
  s.sleepSec = sp.getUShort("sleep", 120);
  s.clicks = sp.getBool("clicks", false);
  s.skipBoot = sp.getBool("skipboot", false);
  s.scanOnBoot = sp.getBool("scanboot", true);
  s.homeChannel = sp.getUChar("homech", LINK_HOME_CHANNEL);
  if (s.backlight < 5) s.backlight = 5;
}
void save() {
  sp.putUChar("bl", s.backlight);
  sp.putUShort("sleep", s.sleepSec);
  sp.putBool("clicks", s.clicks);
  sp.putBool("skipboot", s.skipBoot);
  sp.putBool("scanboot", s.scanOnBoot);
  sp.putUChar("homech", s.homeChannel);
}
}  // namespace settings
