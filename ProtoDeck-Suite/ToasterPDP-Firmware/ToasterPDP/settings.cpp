#include "settings.h"
#include <Preferences.h>

namespace {
Settings s;
Preferences prefs;
constexpr uint16_t MAGIC = 0x5D02;  // bump when the struct layout changes
}  // namespace

namespace settings {

Settings &get() { return s; }

void defaults() {
  memset(&s, 0, sizeof(s));
  s.magic = MAGIC;
  strncpy(s.name, PDP_DEFAULT_NAME, sizeof(s.name) - 1);
  s.chem = BAT_DEFAULT_CHEM;
  s.cells = BAT_DEFAULT_CELLS;
  s.capacityMah = BAT_DEFAULT_MAH;
  s.rintMilliOhm = BAT_DEFAULT_RINT_MO;
  s.vinGain = 1.0f;
  s.inFrontend = ISENSE_IN_FRONTEND;
  s.outFrontend = ISENSE_OUT_FRONTEND;
  s.inZeroMv = ACS_ZERO_MV * s.inFrontend;
  s.outZeroMv = ACS_ZERO_MV * s.outFrontend;
  s.inGain = 1.0f;
  s.outGain = 1.0f;
  s.limInA = LIMIT_IN_A;
  s.limOutA = LIMIT_OUT_A;
  s.limTempC = LIMIT_TEMP_C;
  s.fanMode = FAN_MANUAL;
  s.fanDuty = 100;  // a 4-pin fan also runs flat out with no signal, so this matches "not connected"
  s.fanMin = 30;
  s.fanT0 = 40;
  s.fanT1 = 70;
  s.ledCount = LED_DEFAULT_EXTERNAL;
  s.statusBright = LED_STATUS_BRIGHT;
  s.strip = {PL_ZONE_AUX, PL_FX_ANIM, 80, 128, 0x22, 0xE4, 0xFF, 0x00, 0x00, 0x00};
  s.channel = ESPNOW_CHANNEL;
  s.streamMs = 0;
}

void load() {
  prefs.begin("pdp", false);
  if (prefs.getBytesLength("cfg") == sizeof(Settings)) {
    prefs.getBytes("cfg", &s, sizeof(s));
    if (s.magic == MAGIC) {
      s.name[sizeof(s.name) - 1] = 0;
      if (s.cells == 0) s.cells = BAT_DEFAULT_CELLS;
      if (s.ledCount > LED_MAX_EXTERNAL) s.ledCount = LED_MAX_EXTERNAL;
      if (s.channel < 1 || s.channel > 13) s.channel = ESPNOW_CHANNEL;
      return;
    }
  }
  defaults();
}

void save() { prefs.putBytes("cfg", &s, sizeof(s)); }

}  // namespace settings

namespace socstore {
bool load(float &socPct, float &atVolts) {
  if (!prefs.isKey("soc")) return false;
  socPct = prefs.getFloat("soc", -1);
  atVolts = prefs.getFloat("socv", 0);
  return socPct >= 0 && socPct <= 100;
}
void save(float socPct, float atVolts) {
  prefs.putFloat("soc", socPct);
  prefs.putFloat("socv", atVolts);
}
}  // namespace socstore
