#include "cli.h"
#include <Wire.h>
#include <vector>
#include "config.h"
#include "fan.h"
#include "leds.h"
#include "pdplink.h"
#include "power.h"
#include "sensors.h"
#include "settings.h"
#include "src/protolink/protolink.h"

namespace cli {
namespace {

struct Port {
  Stream  *s = nullptr;
  String   line;
  uint32_t watchMs = 0, lastWatch = 0;
};
Port ports[2];
uint32_t lastStream = 0;

std::vector<String> split(const String &line) {
  std::vector<String> v;
  String cur;
  for (size_t i = 0; i < line.length(); i++) {
    char c = line[i];
    if (c == ' ' || c == '\t') {
      if (cur.length()) v.push_back(cur), cur = "";
    } else cur += c;
  }
  if (cur.length()) v.push_back(cur);
  return v;
}

bool isNum(const String &s) {
  if (!s.length()) return false;
  for (size_t i = 0; i < s.length(); i++)
    if (!isDigit(s[i]) && s[i] != '.' && !(i == 0 && s[i] == '-')) return false;
  return true;
}

int fxFrom(String s) {
  s.toUpperCase();
  if (s == "GAUGE" || s == "BATTERY" || s == "NATIVE") return PL_FX_ANIM;
  for (int i = 0; i < PL_FX_COUNT; i++)
    if (s == plFxName(i)) return i;
  return -1;
}

bool colorFrom(String t, uint8_t &r, uint8_t &g, uint8_t &b) {
  t.toLowerCase();
  struct N { const char *n; uint32_t c; };
  static const N names[] = {{"red", 0xFF1020},   {"green", 0x3DFF7A}, {"blue", 0x1060FF},  {"cyan", 0x22E4FF},
                            {"pink", 0xFF3D8B},  {"purple", 0x7A00FF}, {"white", 0xFFFFFF}, {"orange", 0xFF6A00},
                            {"yellow", 0xFFD000}, {"black", 0},        {"off", 0}};
  for (auto &n : names)
    if (t == n.n) { r = n.c >> 16; g = n.c >> 8; b = n.c; return true; }
  if (t.startsWith("#")) t = t.substring(1);
  if (t.startsWith("0x")) t = t.substring(2);
  if (t.length() != 6) return false;
  uint32_t v = strtoul(t.c_str(), nullptr, 16);
  r = v >> 16; g = v >> 8; b = v;
  return true;
}

void oneLine(Print &o) {
  const Readings &r = sensors::get();
  float soc = power::socPct();
  if (r.batteryPresent)
    o.printf("VIN %5.2fV  IN %5.2fA %5.1fW | 5V %5.2fA %5.1fW | BAT %3s%% %-6s | FAN %3u%% | %4.1fC%s\r\n", r.vin, r.iin,
             r.pin, r.iout, r.pout, soc < 0 ? "--" : String((int)(soc + 0.5f)).c_str(), power::runtimeStr().c_str(),
             fan::dutyPct(), r.tempC, power::alarms() & ~ALM_NO_BATTERY ? "  !" : "");
  else
    o.printf("USB POWER (no battery) | 5V %5.2fA %5.1fW | FAN %3u%% | %4.1fC\r\n", r.iout, r.pout, fan::dutyPct(), r.tempC);
}

void help(Print &o) {
  o.print(
      "ToasterPDP commands\r\n"
      "  status | s            full dashboard            json        one JSON telemetry line\r\n"
      "  watch [ms] | watch off  live one-line readout    stats       peaks / energy\r\n"
      "  stream <ms> | stream off  JSON lines on the J2 SERIAL header (saved)\r\n"
      "BATTERY\r\n"
      "  bat                   show                      bat <li-ion|lifepo4|lead-acid> <cells> <mAh>\r\n"
      "  bat soc <0-100>       set the gauge             bat full    (= bat soc 100)\r\n"
      "  rint <milliohm>       pack resistance for sag compensation\r\n"
      "CALIBRATION   (see README for the procedure)\r\n"
      "  cal                   show                      raw         live pin millivolts\r\n"
      "  cal zero              with no load on 5V/VCC    cal vin <volts measured with a meter>\r\n"
      "  cal in <amps>         cal out <amps>            (with a known steady load)\r\n"
      "  cal div <in|out> <ratio>  if you add a divider in front of a sensor\r\n"
      "  cal reset\r\n"
      "LIMITS / FAN / LEDS\r\n"
      "  limit in|out <amps>   limit temp <C>            (warnings only)\r\n"
      "  fan <0-100>           fan auto [min t0 t1]      fan\r\n"
      "  leds <count>          LEDs chained on P1        ledtest\r\n"
      "  fx <effect> [c1] [c2] [speed]   effects: gauge off solid breathe rainbow chase scanner\r\n"
      "                        sparkle gradient strobe plasma   colours: #RRGGBB or names\r\n"
      "  bri strip|status <0-255>        id          blink the status LED\r\n"
      "SYSTEM\r\n"
      "  name <text>   ch <1-13>   i2c   save   defaults   reset stats   reboot   version\r\n");
}

void showBat(Print &o) {
  const Settings &s = settings::get();
  const pdpmath::Chem &c = pdpmath::chem(s.chem);
  o.printf("battery: %uS %s, %lu mAh, Rint %u mOhm\r\n", s.cells, c.name, (unsigned long)s.capacityMah, s.rintMilliOhm);
  o.printf("  per cell: empty %.2f  warn %.2f  crit %.2f  full %.2f V  ->  pack %.1f / %.1f / %.1f / %.1f V\r\n", c.emptyV,
           c.warnV, c.critV, c.fullV, c.emptyV * s.cells, c.warnV * s.cells, c.critV * s.cells, c.fullV * s.cells);
}

void showCal(Print &o) {
  const Settings &s = settings::get();
  const Readings &r = sensors::get();
  o.printf("vin gain %.4f            (pin %.0f mV -> %.2f V)\r\n", s.vinGain, r.vinMv, r.vin);
  o.printf("in : zero %.1f mV  gain %.4f  frontend %.4f  (pin %.0f mV -> %.3f A, readable to %.1f A)\r\n", s.inZeroMv,
           s.inGain, s.inFrontend, r.inMv, r.iin, r.inMaxReadableA);
  o.printf("out: zero %.1f mV  gain %.4f  frontend %.4f  (pin %.0f mV -> %.3f A, readable to %.1f A)\r\n", s.outZeroMv,
           s.outGain, s.outFrontend, r.outMv, r.iout, r.outMaxReadableA);
}

void cmdCal(const std::vector<String> &a, Print &o) {
  Settings &s = settings::get();
  String sub = a.size() > 1 ? a[1] : "";
  sub.toLowerCase();
  if (sub == "") { showCal(o); return; }
  if (sub == "reset") {
    s.vinGain = s.inGain = s.outGain = 1.0f;
    s.inFrontend = ISENSE_IN_FRONTEND;
    s.outFrontend = ISENSE_OUT_FRONTEND;
    s.inZeroMv = ACS_ZERO_MV * s.inFrontend;
    s.outZeroMv = ACS_ZERO_MV * s.outFrontend;
    settings::save();
    o.print("calibration reset to datasheet values\r\n");
    return;
  }
  if (sub == "div" && a.size() > 3) {
    float ratio = a[3].toFloat();
    if (ratio < 0.2f || ratio > 1.0f) { o.print("ratio must be 0.2-1.0 (bottom / (top + bottom))\r\n"); return; }
    bool in = a[2].equalsIgnoreCase("in");
    (in ? s.inFrontend : s.outFrontend) = ratio;
    (in ? s.inZeroMv : s.outZeroMv) = ACS_ZERO_MV * ratio;
    settings::save();
    o.printf("%s sensor divider %.4f - now run 'cal zero'\r\n", in ? "in" : "out", ratio);
    return;
  }
  float vinMv, inMv, outMv;
  o.print("measuring for 2 s...\r\n");
  sensors::averagePins(2000, vinMv, inMv, outMv);
  if (sub == "zero") {
    float vin = pdpmath::vinFromMv(vinMv, VIN_DIVIDER, s.vinGain);
    // the XIAO itself always draws through the 5 V sensor (and a little from the battery)
    float selfOut = SELF_CURRENT_A;
    float selfIn = vin > 3 ? SELF_CURRENT_A * VOUT_NOMINAL / (vin * 0.85f) : 0;
    float zIn = inMv - selfIn * ACS_MV_PER_A * s.inFrontend * s.inGain;
    float zOut = outMv - selfOut * ACS_MV_PER_A * s.outFrontend * s.outGain;
    float nomIn = ACS_ZERO_MV * s.inFrontend, nomOut = ACS_ZERO_MV * s.outFrontend;
    bool okIn = fabsf(zIn - nomIn) < 150 * s.inFrontend, okOut = fabsf(zOut - nomOut) < 150 * s.outFrontend;
    if (okIn && vin > 3) s.inZeroMv = zIn;
    if (okOut) s.outZeroMv = zOut;
    settings::save();
    o.printf("in  zero %.1f mV %s\r\n", zIn, vin <= 3 ? "(skipped: no battery)" : okIn ? "saved" : "REJECTED - load connected?");
    o.printf("out zero %.1f mV %s\r\n", zOut, okOut ? "saved" : "REJECTED - load connected?");
    return;
  }
  if (a.size() < 3) { o.print("cal vin|in|out <value>\r\n"); return; }
  float ref = a[2].toFloat();
  if (sub == "vin") {
    float raw = vinMv * 0.001f * VIN_DIVIDER;
    if (ref < 3 || raw < 1) { o.print("need a battery connected and a value in volts\r\n"); return; }
    s.vinGain = ref / raw;
    settings::save();
    o.printf("vin gain %.4f saved\r\n", s.vinGain);
  } else if (sub == "in" || sub == "out") {
    bool in = sub == "in";
    float mv = in ? inMv : outMv, zero = in ? s.inZeroMv : s.outZeroMv, fe = in ? s.inFrontend : s.outFrontend;
    float d = mv - zero;
    if (ref < 0.5f || d < 50) { o.print("use a steady load of at least 0.5 A\r\n"); return; }
    float g = d / (ref * ACS_MV_PER_A * fe);
    if (g < 0.8f || g > 1.2f) { o.printf("gain %.3f is >20%% off - check the load / zero first\r\n", g); return; }
    (in ? s.inGain : s.outGain) = g;
    settings::save();
    o.printf("%s gain %.4f saved\r\n", in ? "in" : "out", g);
  } else {
    o.print("cal [zero | vin <V> | in <A> | out <A> | div in|out <ratio> | reset]\r\n");
  }
}

void cmdBat(const std::vector<String> &a, Print &o) {
  Settings &s = settings::get();
  if (a.size() == 1) { showBat(o); return; }
  String sub = a[1];
  sub.toLowerCase();
  if (sub == "soc" && a.size() > 2) {
    power::setSoc(a[2].toFloat());
    power::saveSoc();
    o.printf("gauge set to %.0f%%\r\n", power::socPct());
    return;
  }
  if (sub == "full") {
    power::setSoc(100);
    power::saveSoc();
    o.print("gauge set to 100%\r\n");
    return;
  }
  int chem = -1;
  for (int i = 0; i < pdpmath::N_CHEMS; i++)
    if (sub == pdpmath::CHEMS[i].name) chem = i;
  if (sub == "liion" || sub == "lipo") chem = 0;
  if (sub == "lfp") chem = 1;
  if (sub == "sla" || sub == "lead") chem = 2;
  if (chem < 0 || a.size() < 4) { o.print("bat <li-ion|lifepo4|lead-acid> <cells in series> <mAh>\r\n"); return; }
  int cells = a[2].toInt();
  long mah = a[3].toInt();
  if (cells < 1 || cells > 16 || mah < 100) { o.print("cells 1-16, capacity >= 100 mAh\r\n"); return; }
  s.chem = chem;
  s.cells = cells;
  s.capacityMah = mah;
  settings::save();
  power::reseed();  // re-estimate from voltage with the new profile
  showBat(o);
}

void cmdFan(const std::vector<String> &a, Print &o) {
  Settings &s = settings::get();
  if (a.size() > 1) {
    if (a[1].equalsIgnoreCase("auto")) {
      s.fanMode = FAN_AUTO;
      if (a.size() > 4) {
        s.fanMin = constrain(a[2].toInt(), 0, 100);
        s.fanT0 = constrain(a[3].toInt(), 0, 120);
        s.fanT1 = constrain(a[4].toInt(), s.fanT0 + 1, 125);
      }
    } else if (a[1].equalsIgnoreCase("off")) {
      s.fanMode = FAN_MANUAL;
      s.fanDuty = 0;
    } else if (isNum(a[1])) {
      s.fanMode = FAN_MANUAL;
      s.fanDuty = constrain(a[1].toInt(), 0, 100);
    }
    settings::save();
  }
  o.printf("fan %s, output %u%%", s.fanMode == FAN_AUTO ? "auto" : "manual", fan::dutyPct());
  if (s.fanMode == FAN_AUTO) o.printf(" (min %u%%, ramp %u-%u C or 0-30 W on 5V)", s.fanMin, s.fanT0, s.fanT1);
  if (fan::rpm() >= 0) o.printf(", %ld rpm", (long)fan::rpm());
  o.print("\r\n");
}

void cmdFx(const std::vector<String> &a, Print &o) {
  Settings &s = settings::get();
  if (a.size() < 2) { o.printf("strip fx %s, bri %u, %u LEDs\r\n", plFxName(s.strip.fx), s.strip.brightness, s.ledCount); return; }
  int e = fxFrom(a[1]);
  if (e < 0) { o.print("unknown effect\r\n"); return; }
  PlLedFx f = s.strip;
  f.fx = e;
  if (a.size() > 2 && !colorFrom(a[2], f.r1, f.g1, f.b1)) { o.print("colour: #RRGGBB or a name\r\n"); return; }
  if (a.size() > 3) colorFrom(a[3], f.r2, f.g2, f.b2);
  if (a.size() > 4) f.speed = constrain(a[4].toInt(), 0, 255);
  s.strip = f;
  settings::save();
  o.printf("strip -> %s%s\r\n", plFxName(e), e == PL_FX_ANIM ? " (battery gauge)" : "");
  if (!s.ledCount) o.print("note: 0 LEDs configured on P1 - set with 'leds <n>'\r\n");
}

void i2cScan(Print &o) {
  int n = 0;
  o.print("I2C (J1, SDA D4 / SCL D5):");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      o.printf(" 0x%02X", addr);
      n++;
    }
  }
  o.printf("%s\r\n", n ? "" : " nothing found");
}

void stats(Print &o) {
  const Readings &r = sensors::get();
  const pdpmath::Gauge &g = power::gauge();
  uint32_t s = millis() / 1000;
  o.printf("uptime %02lu:%02lu:%02lu\r\n", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
  o.printf("battery side: peak %.2f A, VIN min %.2f / max %.2f V\r\n", r.iinPeak, r.vinMin, r.vinMax);
  o.printf("5 V rail:     peak %.2f A (%.1f W)\r\n", r.ioutPeak, r.ioutPeak * VOUT_NOMINAL);
  o.printf("used:         %.0f mAh, %.2f Wh this session\r\n", g.usedMah, g.usedWh);
}

}  // namespace

void json(Print &o) {
  const Readings &r = sensors::get();
  const pdpmath::Gauge &g = power::gauge();
  o.printf(
      "{\"t\":%lu,\"vin\":%.3f,\"iin\":%.3f,\"pin\":%.2f,\"iout\":%.3f,\"pout\":%.2f,\"soc\":%.1f,\"rt\":%ld,"
      "\"cell\":%.3f,\"temp\":%.1f,\"fan\":%u,\"rpm\":%ld,\"mah\":%.0f,\"wh\":%.3f,\"alm\":%u}\r\n",
      (unsigned long)millis(), r.vin, r.iin, r.pin, r.iout, r.pout, power::socPct(), (long)power::runtimeMin(),
      power::cellVolts(), r.tempC, fan::dutyPct(), (long)fan::rpm(), g.usedMah, g.usedWh, power::alarms());
}

void status(Print &o) {
  const Readings &r = sensors::get();
  const Settings &s = settings::get();
  const pdpmath::Chem &c = pdpmath::chem(s.chem);
  uint32_t up = millis() / 1000;
  uint32_t deck = pdplink::lastDeckMs();
  o.printf("%s  v%s  up %02lu:%02lu:%02lu  ESP-NOW ch %u  deck %s\r\n", s.name, PDP_FW_VERSION, (unsigned long)(up / 3600),
           (unsigned long)(up / 60 % 60), (unsigned long)(up % 60), s.channel,
           !pdplink::started() ? "radio off" : deck ? (String((millis() - deck) / 1000) + "s ago").c_str() : "not seen");
  if (r.batteryPresent) {
    float soc = power::socPct();
    o.printf("BATTERY   %6.2f V  (%.3f V/cell, %uS %s)  %s%%  ~%s left\r\n", r.vin, power::cellVolts(), s.cells, c.name,
             soc < 0 ? "--" : String(soc, 0).c_str(), power::runtimeStr().c_str());
    o.printf("  draw    %6.2f A  %6.1f W      peak %.2f A   min %.2f V\r\n", r.iin, r.pin, r.iinPeak, r.vinMin);
  } else {
    o.print("BATTERY   not connected - running from USB\r\n");
  }
  o.printf("5V RAIL   %6.2f A  %6.1f W      peak %.2f A%s\r\n", r.iout, r.pout, r.ioutPeak,
           r.outNearLimit ? "   ** SENSOR NEAR ADC LIMIT **" : "");
  if (r.batteryPresent && r.pin > 0.5f) {
    float other = r.pin - r.pout / 0.92f;
    o.printf("  other   ~%5.1f W  (VCC/24 V loads + regulator loss)\r\n", other < 0 ? 0 : other);
  }
  o.printf("FAN       %3u%% %s", fan::dutyPct(), s.fanMode == FAN_AUTO ? "(auto)" : "(manual)");
  if (fan::rpm() >= 0) o.printf("  %ld rpm", (long)fan::rpm());
  o.printf("      CHIP %.1f C\r\n", r.tempC);
  o.printf("LEDS      status bri %u, %u on P1 fx %s bri %u (~%.0f mA)\r\n", s.statusBright, s.ledCount,
           s.strip.fx == PL_FX_ANIM ? "GAUGE" : plFxName(s.strip.fx), s.strip.brightness, leds::estimateStripmA());
  o.printf("ENERGY    %.0f mAh / %.2f Wh used this session\r\n", power::gauge().usedMah, power::gauge().usedWh);
  o.printf("ALARMS    %s\r\n", power::alarmList(power::alarms()).c_str());
}

void exec(const String &raw, Print &o) {
  String line = raw;
  line.trim();
  if (!line.length()) return;
  std::vector<String> a = split(line);
  String c = a[0];
  c.toLowerCase();
  Settings &s = settings::get();

  if (c == "help" || c == "?") help(o);
  else if (c == "status" || c == "s") status(o);
  else if (c == "json") json(o);
  else if (c == "watch") {
    Port *p = nullptr;
    for (auto &pt : ports)
      if ((Print *)pt.s == &o) p = &pt;
    if (!p) { o.print("watch only works on a serial port (on the deck use: tele watch)\r\n"); return; }
    p->watchMs = (a.size() > 1 && a[1] == "off") ? 0 : (a.size() > 1 ? constrain(a[1].toInt(), 100L, 60000L) : 500);
    o.printf("watch %s\r\n", p->watchMs ? (String(p->watchMs) + " ms").c_str() : "off");
  } else if (c == "stream") {
    s.streamMs = (a.size() > 1 && a[1] != "off") ? constrain(a[1].toInt(), 100L, 60000L) : 0;
    settings::save();
    o.printf("J2 SERIAL JSON stream %s\r\n", s.streamMs ? (String(s.streamMs) + " ms").c_str() : "off");
  } else if (c == "bat" || c == "battery") cmdBat(a, o);
  else if (c == "rint") {
    if (a.size() > 1) { s.rintMilliOhm = constrain(a[1].toInt(), 0, 5000); settings::save(); }
    o.printf("pack resistance %u mOhm\r\n", s.rintMilliOhm);
  } else if (c == "cal") cmdCal(a, o);
  else if (c == "raw") {
    const Readings &r = sensors::get();
    o.printf("pins: VIN_DIV %.1f mV   I_SENSE %.1f mV   I_SENSE_1 %.1f mV\r\n", r.vinMv, r.inMv, r.outMv);
  } else if (c == "limit" || c == "limits") {
    if (a.size() > 2) {
      float v = a[2].toFloat();
      if (a[1] == "in") s.limInA = v;
      else if (a[1] == "out") s.limOutA = v;
      else if (a[1] == "temp") s.limTempC = v;
      settings::save();
    }
    o.printf("warn above: in %.1f A, out %.1f A, chip %.0f C\r\n", s.limInA, s.limOutA, s.limTempC);
  } else if (c == "fan") cmdFan(a, o);
  else if (c == "leds") {
    if (a.size() > 1) {
      s.ledCount = constrain(a[1].toInt(), 0, LED_MAX_EXTERNAL);
      leds::setCount(s.ledCount);
      settings::save();
    }
    o.printf("%u LEDs on P1 (plus the onboard status LED)\r\n", s.ledCount);
  } else if (c == "ledtest") {
    leds::test();
    o.print("done\r\n");
  } else if (c == "fx") cmdFx(a, o);
  else if (c == "bri") {
    if (a.size() > 2) {
      uint8_t v = constrain(a[2].toInt(), 0, 255);
      if (a[1] == "status") s.statusBright = v;
      else s.strip.brightness = v;
      settings::save();
    }
    o.printf("brightness: status %u, strip %u\r\n", s.statusBright, s.strip.brightness);
  } else if (c == "id" || c == "identify") {
    leds::identify(2000);
    o.print("blinking\r\n");
  } else if (c == "name") {
    if (a.size() > 1) {
      int sp = line.indexOf(' ');
      String n = line.substring(sp + 1);
      n.trim();
      strncpy(s.name, n.c_str(), sizeof(s.name) - 1);
      s.name[sizeof(s.name) - 1] = 0;
      settings::save();
      o.print("saved - reboot to show it on the deck\r\n");
    }
    o.printf("name %s\r\n", s.name);
  } else if (c == "ch" || c == "channel") {
    if (a.size() > 1) {
      int ch = a[1].toInt();
      if (ch < 1 || ch > 13) { o.print("channel 1-13\r\n"); return; }
      s.channel = ch;
      settings::save();
      o.print("saved - rebooting to switch channel\r\n");
      delay(200);
      ESP.restart();
    }
    o.printf("ESP-NOW channel %u\r\n", s.channel);
  } else if (c == "i2c") i2cScan(o);
  else if (c == "stats") stats(o);
  else if (c == "reset" && a.size() > 1 && a[1] == "stats") {
    power::resetSession();
    o.print("session stats cleared\r\n");
  } else if (c == "save") {
    settings::save();
    power::saveSoc();
    o.print("saved\r\n");
  } else if (c == "defaults") {
    settings::defaults();
    settings::save();
    leds::setCount(settings::get().ledCount);
    o.print("factory defaults restored (calibration too)\r\n");
  } else if (c == "reboot") {
    power::saveSoc();
    o.print("rebooting\r\n");
    delay(200);
    ESP.restart();
  } else if (c == "version" || c == "about") {
    o.printf("ToasterPDP v%s - XIAO ESP32-C6 on the DIY-Toaster power distribution board\r\n", PDP_FW_VERSION);
  } else {
    o.printf("unknown '%s' - type help\r\n", c.c_str());
  }
}

void broadcast(const char *line) {
  for (auto &p : ports)
    if (p.s) { p.s->print(line); p.s->print("\r\n"); }
}

void begin(Stream &usb, Stream &ext) {
  ports[0].s = &usb;
  ports[1].s = &ext;
}

void loop() {
  uint32_t now = millis();
  for (auto &p : ports) {
    if (!p.s) continue;
    while (p.s->available()) {
      char ch = p.s->read();
      if (ch == '\r' || ch == '\n') {
        if (p.line.length()) exec(p.line, *p.s);
        p.line = "";
      } else if (ch == 8 || ch == 127) {
        if (p.line.length()) p.line.remove(p.line.length() - 1);
      } else if (p.line.length() < 160) {
        p.line += ch;
      }
    }
    if (p.watchMs && now - p.lastWatch >= p.watchMs) {
      p.lastWatch = now;
      oneLine(*p.s);
    }
  }
  const Settings &s = settings::get();
  if (s.streamMs && ports[1].s && now - lastStream >= s.streamMs) {
    lastStream = now;
    json(*ports[1].s);
  }
}

}  // namespace cli
