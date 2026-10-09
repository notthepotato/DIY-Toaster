#include "power.h"
#include "config.h"
#include "sensors.h"
#include "settings.h"

namespace power {
namespace {

pdpmath::Gauge g;
pdpmath::Debounce deb[ALARM_COUNT];
uint16_t active = 0;
uint32_t lastUpdate = 0, lastSave = 0, presentSince = 0;
bool wasPresent = false;
AlarmCb cb = nullptr;

const char *NAMES[ALARM_COUNT] = {"NO BATTERY (USB power)", "BATTERY LOW", "BATTERY CRITICAL", "BATTERY OVER-VOLTAGE",
                                  "INPUT VOLTAGE LOW (5V dropout)", "INPUT OVER-CURRENT", "5V OVER-CURRENT",
                                  "IN SENSOR NEAR ADC LIMIT", "OUT SENSOR NEAR ADC LIMIT", "OVER-TEMPERATURE"};

void setAlarm(int bit, bool want, uint32_t now, uint32_t onMs, uint32_t offMs) {
  if (deb[bit].update(want, now, onMs, offMs)) {
    uint16_t m = 1u << bit;
    if (deb[bit].state) active |= m;
    else active &= ~m;
    if (cb) cb(m, deb[bit].state);
  }
}

}  // namespace

const pdpmath::Gauge &gauge() { return g; }
float socPct() { return g.soc; }
uint16_t alarms() { return active; }
bool critical() {
  return active & (ALM_BAT_CRIT | ALM_BAT_HIGH | ALM_IN_OVER | ALM_OUT_OVER | ALM_OUT_LIMIT | ALM_TEMP);
}
void onAlarm(AlarmCb f) { cb = f; }

float cellVolts() {
  const Settings &s = settings::get();
  return sensors::get().vin / (s.cells ? s.cells : 1);
}

int32_t runtimeMin() { return g.runtimeMin(settings::get().capacityMah); }

String runtimeStr() {
  int32_t m = runtimeMin();
  if (m < 0) return "--";
  char b[16];
  if (m >= 99 * 60) snprintf(b, sizeof(b), ">99h");
  else snprintf(b, sizeof(b), "%ldh%02ldm", (long)(m / 60), (long)(m % 60));
  return String(b);
}

const char *alarmName(int bit) { return bit >= 0 && bit < ALARM_COUNT ? NAMES[bit] : "?"; }

String alarmList(uint16_t mask) {
  String s;
  for (int i = 0; i < ALARM_COUNT; i++)
    if (mask & (1u << i)) {
      if (s.length()) s += ", ";
      s += NAMES[i];
    }
  return s.length() ? s : String("none");
}

void setSoc(float pct) { g.soc = constrain(pct, 0.0f, 100.0f); }
void reseed() {
  g.soc = -1;
  socstore::save(-1, 0);  // don't let the old stored value win
  presentSince = millis();
}

void resetSession() {
  g.usedMah = 0;
  g.usedWh = 0;
  sensors::resetStats();
}

void saveSoc() {
  if (g.soc >= 0) socstore::save(g.soc, sensors::get().vin);
  lastSave = millis();
}

void begin() { lastUpdate = millis(); }

void update() {
  const Readings &r = sensors::get();
  const Settings &s = settings::get();
  const pdpmath::Chem &c = pdpmath::chem(s.chem);
  uint32_t now = millis();
  float dt = (now - lastUpdate) * 0.001f;
  lastUpdate = now;
  float rint = s.rintMilliOhm * 0.001f;

  // (re)initialise the gauge 1.5 s after a battery shows up, once the filters settle
  if (r.batteryPresent && !wasPresent) presentSince = now;
  wasPresent = r.batteryPresent;
  if (!r.batteryPresent) {
    g.soc = -1;
  } else if (g.soc < 0 && now - presentSince > 1500) {
    float stored = -1, atV = 0;
    socstore::load(stored, atV);
    float tol = c.trustVoltage < 0.5f ? 30.0f : 15.0f;  // flat LiFePO4 curve: be more forgiving
    g.begin(c, s.cells, r.vin, r.iin, rint, stored, tol);
    lastSave = now;
  } else if (g.soc >= 0) {
    g.update(c, s.cells, s.capacityMah, r.vin, r.iin, rint, dt);
    if (now - lastSave > SOC_SAVE_PERIOD_MS) saveSoc();
  }

  float cell = r.vin / (s.cells ? s.cells : 1);
  bool bat = r.batteryPresent;
  bool settled = bat && now - presentSince > 3000;
  setAlarm(0, !bat, now, 1500, 500);
  // low/critical: per-cell voltage under load OR gauge %. Low has a wide hysteresis.
  bool low = settled && (cell < c.warnV || (g.soc >= 0 && g.soc <= 20));
  bool lowClear = !settled || (cell > c.warnV + 0.05f && (g.soc < 0 || g.soc >= 25));
  setAlarm(1, deb[1].state ? !lowClear : low, now, 5000, 10000);
  bool crit = settled && (cell < c.critV || (g.soc >= 0 && g.soc <= 5));
  setAlarm(2, crit, now, 3000, 15000);
  setAlarm(3, settled && cell > c.maxV, now, 3000, 3000);
  setAlarm(4, bat && r.vin < 6.0f, now, 1000, 2000);
  setAlarm(5, r.iin > s.limInA, now, 300, 2000);
  setAlarm(6, r.iout > s.limOutA, now, 300, 2000);
  setAlarm(7, r.inNearLimit, now, 0, 3000);
  setAlarm(8, r.outNearLimit, now, 0, 3000);
  setAlarm(9, !isnan(r.tempC) && r.tempC > s.limTempC, now, 2000, 10000);
}

}  // namespace power
