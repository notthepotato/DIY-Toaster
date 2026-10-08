#include "console.h"
#include "board.h"
#include "config.h"
#include "presets.h"
#include <math.h>
#include <stdarg.h>

namespace console {
namespace {

Sink    sinks[3] = {nullptr};
ClearFn clearFn = nullptr;
std::vector<String> hist;
String  serialLine;
bool    ansi = false;
bool    watchTele = false;
int     teleSeq = -1, animSeq = -1, pingSeq = -1;

void out(uint32_t color, const char *text) {
  // Serial: optional ANSI truecolor
  if (ansi) Serial.printf("\x1b[38;2;%u;%u;%um", (unsigned)(color >> 16) & 0xFF, (unsigned)(color >> 8) & 0xFF,
                          (unsigned)color & 0xFF);
  Serial.print(text);
  if (ansi) Serial.print("\x1b[0m");
  Serial.print("\r\n");
  for (auto s : sinks)
    if (s) s(text, color);
}

std::vector<String> split(const String &line) {
  std::vector<String> v;
  String cur;
  bool q = false;
  for (size_t i = 0; i < line.length(); i++) {
    char c = line[i];
    if (c == '"') { q = !q; continue; }
    if (c == ' ' && !q) {
      if (cur.length()) v.push_back(cur), cur = "";
    } else cur += c;
  }
  if (cur.length()) v.push_back(cur);
  return v;
}

int zoneFrom(const String &s) {
  String t = s;
  t.toLowerCase();
  if (t == "all" || t == "*") return PL_ZONE_ALL;
  if (t == "visor" || t == "v" || t == "face") return PL_ZONE_VISOR;
  if (t == "ears" || t == "ear" || t == "e") return PL_ZONE_EARS;
  if (t == "aux" || t == "strip" || t == "a") return PL_ZONE_AUX;
  return -1;
}

int fxFrom(const String &s) {
  String t = s;
  t.toUpperCase();
  for (int i = 0; i < PL_FX_COUNT; i++)
    if (t == plFxName(i)) return i;
  if (t == "NATIVE") return PL_FX_ANIM;
  bool num = t.length() > 0;
  for (char c : t)
    if (!isDigit(c)) num = false;
  if (num && t.toInt() < PL_FX_COUNT) return t.toInt();
  return -1;
}

bool colorFrom(const String &s, uint8_t &r, uint8_t &g, uint8_t &b) {
  String t = s;
  t.toLowerCase();
  struct Named { const char *n; uint32_t c; };
  static const Named names[] = {{"red", 0xFF1020},   {"green", 0x3DFF7A}, {"blue", 0x1060FF},  {"cyan", 0x22E4FF},
                                {"pink", 0xFF3D8B},  {"purple", 0x7A00FF}, {"white", 0xFFFFFF}, {"orange", 0xFF6A00},
                                {"yellow", 0xFFD000}, {"black", 0x000000}, {"off", 0x000000}};
  for (auto &n : names)
    if (t == n.n) { r = n.c >> 16; g = n.c >> 8; b = n.c; return true; }
  if (t.startsWith("#")) t = t.substring(1);
  if (t.startsWith("0x")) t = t.substring(2);
  if (t.length() != 6) return false;
  uint32_t v = strtoul(t.c_str(), nullptr, 16);
  r = v >> 16; g = v >> 8; b = v;
  return true;
}

uint8_t parseLevel(const String &s) {  // "128", "50%"
  if (s.endsWith("%")) return (uint8_t)constrain(s.substring(0, s.length() - 1).toInt() * 255 / 100, 0, 255);
  return (uint8_t)constrain(s.toInt(), 0, 255);
}

int target() {
  int p = radio::selected();
  if (p < 0) print(CON_WARN, "no node selected - run 'scan' then 'sel <n>'");
  return p;
}

const char *ago(uint32_t ms) {
  static char b[16];
  uint32_t d = millis() - ms;
  if (!ms) return "never";
  if (d < 1000) snprintf(b, sizeof(b), "now");
  else if (d < 60000) snprintf(b, sizeof(b), "%lus", (unsigned long)(d / 1000));
  else snprintf(b, sizeof(b), "%lum", (unsigned long)(d / 60000));
  return b;
}

String caps(uint32_t c) {
  String s;
  if (c & PL_CAP_ANIMS) s += "ANIM ";
  if (c & PL_CAP_LEDFX) s += "FX ";
  if (c & PL_CAP_POWER) s += "PWR ";
  if (c & PL_CAP_FAN) s += "FAN ";
  if (c & PL_CAP_MIC) s += "MIC ";
  if (c & PL_CAP_TEXT) s += "TXT ";
  s.trim();
  return s;
}

void printTele(const Peer &p) {
  const PlTelemetry &t = p.tele;
  bool est = t.flags & PL_TF_ESTIMATED;
  print(CON_ACCENT, "-- TELEMETRY %s %s", p.hello.name, est ? "(estimated current)" : "");
  char v[16], i[16], w[16], tc[16];
  if (isnan(t.busV)) snprintf(v, sizeof(v), "  --   ");
  else snprintf(v, sizeof(v), "%6.2f V", t.busV);
  if (isnan(t.currentmA)) snprintf(i, sizeof(i), "  --   ");
  else snprintf(i, sizeof(i), "%s%5.2f A", est ? "~" : "", t.currentmA / 1000.0f);
  if (isnan(t.powermW)) snprintf(w, sizeof(w), "  --   ");
  else snprintf(w, sizeof(w), "%s%5.2f W", est ? "~" : "", t.powermW / 1000.0f);
  if (isnan(t.tempC)) snprintf(tc, sizeof(tc), " --");
  else snprintf(tc, sizeof(tc), "%.1f C", t.tempC);
  print(CON_TEXT, " bus   %-10s current %-10s power %s", v, i, w);
  print(CON_TEXT, " anim  %-10s fx      %-10s temp  %s", t.anim, plFxName(t.fx), tc);
  print(CON_TEXT, " bri   visor %3u  ears %3u   fan %3u   mic %3u%%", t.brightVisor, t.brightEars, t.fanDuty,
        t.micLevel);
  uint32_t s = t.uptimeMs / 1000;
  print(CON_DIM, " up    %02lu:%02lu:%02lu   heap %lu KB   rssi %d dBm   rtt %lu ms", (unsigned long)(s / 3600),
        (unsigned long)(s / 60 % 60), (unsigned long)(s % 60), (unsigned long)(t.freeHeap / 1024), p.rssi,
        (unsigned long)p.rttMs);
}

void cmdHelp() {
  static const char *lines[] = {
      "NODES     scan [ch]       ls            sel <n|name>     info      ping",
      "DATA      tele [watch|stop]             anims            stats",
      "FACE      anim <name|next|prev>         visor <custom|rainbow|toggle>",
      "LEDS      fx <zone> <effect> [c1] [c2] [speed] [bri]      off   native",
      "          zones: all visor ears aux    effects: anim off solid breathe rainbow",
      "          chase scanner sparkle gradient strobe plasma   colours: #RRGGBB or names",
      "          bright <zone> <0-255|50%>     fan <0-255>      id (identify)",
      "PRESETS   preset ls | apply <n> | show <n> | set <n> <name> <zone> <fx> <c1> <c2> [spd] [bri]",
      "          preset reset",
      "NODE      say <text>  (raw text cmd, e.g. say ?  /  say status)   save   reboot",
      "DECK      ch [1-13]   bl <0-100>   sleep   beep   clicks on|off   ansi on|off",
      "          clear   history   neofetch   reboot deck",
  };
  print(CON_ACCENT, "PROTO//DECK command set");
  for (auto l : lines) print(CON_TEXT, "%s", l);
}

void cmdLs() {
  int n = radio::count();
  if (!n) { print(CON_DIM, "no nodes yet - try 'scan'"); return; }
  print(CON_DIM, "  #  NAME                KIND        CH  RSSI  SEEN  CAPS");
  for (int i = 0; i < n; i++) {
    Peer *p = radio::peer(i);
    print(p->online() ? CON_TEXT : CON_DIM, "%c%2d  %-19s %-11s %2u  %4d  %-5s %s", i == radio::selected() ? '>' : ' ',
          i, p->hello.name, p->hello.kind, p->hello.channel, p->rssi, ago(p->lastSeen), caps(p->hello.caps).c_str());
  }
}

void sendFx(int p, PlLedFx fx, const char *label) {
  if (radio::ledFx(p, fx) < 0) print(CON_ERR, "send failed");
  else if (label) print(CON_DIM, "-> %s", label);
}

void cmdPreset(const std::vector<String> &a) {
  String sub = a.size() > 1 ? a[1] : "ls";
  if (sub == "ls" || sub == "list") {
    for (int i = 0; i < presets::count(); i++) print(CON_TEXT, "%2d  %s", i, presets::describe(presets::get(i)).c_str());
  } else if ((sub == "apply" || sub == "show") && a.size() > 2) {
    int i = a[2].toInt();
    if (i < 0 || i >= presets::count()) { print(CON_ERR, "slot 0-%d", presets::count() - 1); return; }
    Preset &pr = presets::get(i);
    if (sub == "show") { print(CON_TEXT, "%s", presets::describe(pr).c_str()); return; }
    int p = target();
    if (p >= 0) sendFx(p, pr.fx, pr.name);
  } else if (sub == "set" && a.size() >= 7) {
    int i = a[2].toInt();
    Preset pr = {};
    strncpy(pr.name, a[3].c_str(), sizeof(pr.name) - 1);
    String up = String(pr.name);
    up.toUpperCase();
    strncpy(pr.name, up.c_str(), sizeof(pr.name) - 1);
    int z = zoneFrom(a[4]), f = fxFrom(a[5]);
    if (z < 0 || f < 0 || !colorFrom(a[6], pr.fx.r1, pr.fx.g1, pr.fx.b1)) { print(CON_ERR, "bad zone/effect/colour"); return; }
    if (a.size() > 7) colorFrom(a[7], pr.fx.r2, pr.fx.g2, pr.fx.b2);
    pr.fx.zone = z;
    pr.fx.fx = f;
    pr.fx.speed = a.size() > 8 ? parseLevel(a[8]) : 128;
    pr.fx.brightness = a.size() > 9 ? parseLevel(a[9]) : 0;
    presets::put(i, pr);
    print(CON_OK, "slot %d = %s", i, presets::describe(pr).c_str());
  } else if (sub == "reset") {
    presets::resetDefaults();
    print(CON_OK, "presets reset to defaults");
  } else {
    print(CON_WARN, "preset ls | apply <n> | show <n> | set ... | reset");
  }
}

}  // namespace

void print(uint32_t color, const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  // split multi-line text
  char *line = buf;
  while (line) {
    char *nl = strchr(line, '\n');
    if (nl) *nl = 0;
    out(color, line);
    line = nl ? nl + 1 : nullptr;
  }
}

void addSink(Sink s) {
  for (auto &x : sinks)
    if (!x) { x = s; return; }
}
void setClear(ClearFn f) { clearFn = f; }
const std::vector<String> &history() { return hist; }

void neofetch() {
  static const char *art[] = {
      "     /\\_____/\\      ",
      "    /  >   <  \\     ",
      "   |  [=====]  |    ",
      "   |   \\___/   |    ",
      "    \\_________/     ",
      "   //  |   |  \\\\    ",
  };
  char info[6][64];
  snprintf(info[0], 64, "%s v%s", DECK_NAME, DECK_VERSION);
  snprintf(info[1], 64, "host  CrowPanel Advance 7.0 rev %u.%u", board::revision() / 10, board::revision() % 10);
  snprintf(info[2], 64, "cpu   ESP32-S3 x2 @ %lu MHz", (unsigned long)ESP.getCpuFreqMHz());
  snprintf(info[3], 64, "mem   %lu KB sram free / %lu KB psram", (unsigned long)(ESP.getFreeHeap() / 1024),
           (unsigned long)(ESP.getPsramSize() / 1024));
  snprintf(info[4], 64, "radio ESP-NOW ch %u  %s", radio::channel(), radio::myMac().c_str());
  snprintf(info[5], 64, "nodes %d known   up %lus", radio::count(), (unsigned long)(millis() / 1000));
  for (int i = 0; i < 6; i++) print(i == 0 ? CON_PINK : CON_ACCENT, "%s %s", art[i], info[i]);
}

void exec(const String &raw, bool echo) {
  String line = raw;
  line.trim();
  if (!line.length()) return;
  if (echo) print(CON_PINK, "> %s", line.c_str());
  if (hist.empty() || hist.back() != line) {
    hist.push_back(line);
    if (hist.size() > 40) hist.erase(hist.begin());
  }
  std::vector<String> a = split(line);
  String c = a[0];
  c.toLowerCase();
  int p;

  if (c == "help" || c == "?") cmdHelp();
  else if (c == "scan") {
    uint8_t ch = a.size() > 1 ? a[1].toInt() : 0;
    if (!radio::startScan(ch)) print(CON_WARN, "scan already running");
  } else if (c == "ls" || c == "nodes") cmdLs();
  else if (c == "sel" || c == "select") {
    if (a.size() < 2) { print(CON_WARN, "sel <index|name|mac-tail>"); return; }
    int i = radio::find(a[1]);
    if (i < 0) { print(CON_ERR, "no node matches '%s'", a[1].c_str()); return; }
    radio::select(i);
    print(CON_OK, "target -> %s", radio::peer(i)->hello.name);
  } else if (c == "info") {
    int i = a.size() > 1 ? radio::find(a[1]) : radio::selected();
    Peer *pr = radio::peer(i);
    if (!pr) { print(CON_WARN, "no such node"); return; }
    print(CON_ACCENT, "-- %s", pr->hello.name);
    print(CON_TEXT, " kind %s   fw %u.%u   mac %s", pr->hello.kind, pr->hello.fwMajor, pr->hello.fwMinor,
          pr->macStr().c_str());
    print(CON_TEXT, " ch %u   rssi %d dBm   rtt %lu ms   seen %s", pr->hello.channel, pr->rssi,
          (unsigned long)pr->rttMs, ago(pr->lastSeen));
    print(CON_TEXT, " leds %u   anims %u   caps %s", pr->hello.ledCount, pr->hello.animCount,
          caps(pr->hello.caps).c_str());
    if (pr->hasTele) printTele(*pr);
  } else if (c == "ping") {
    if ((p = target()) >= 0) pingSeq = radio::ping(p);
  } else if (c == "tele" || c == "telemetry" || c == "power") {
    String m = a.size() > 1 ? a[1] : "";
    if (m == "watch") { watchTele = true; print(CON_DIM, "streaming telemetry ('tele stop' to end)"); }
    else if (m == "stop") { watchTele = false; print(CON_DIM, "telemetry stream stopped"); }
    else if ((p = target()) >= 0) teleSeq = radio::getTelemetry(p);
  } else if (c == "anims") {
    if ((p = target()) >= 0) animSeq = radio::getAnims(p);
  } else if (c == "anim" || c == "play") {
    if ((p = target()) < 0) return;
    if (a.size() < 2) { print(CON_WARN, "anim <name|next|prev>"); return; }
    if (a[1] == "next") radio::command(p, PL_CMD_NEXT_ANIM);
    else if (a[1] == "prev") radio::command(p, PL_CMD_PREV_ANIM);
    else radio::setAnim(p, a[1].c_str());
  } else if (c == "bright" || c == "bri") {
    if ((p = target()) < 0) return;
    if (a.size() < 3) { print(CON_WARN, "bright <all|visor|ears> <0-255|NN%%>"); return; }
    int z = zoneFrom(a[1]);
    if (z < 0) { print(CON_ERR, "zone?"); return; }
    radio::brightness(p, z, parseLevel(a[2]));
  } else if (c == "fan") {
    if ((p = target()) < 0) return;
    uint8_t d = a.size() > 1 ? parseLevel(a[1]) : 255;
    radio::command(p, PL_CMD_FAN, &d, 1);
  } else if (c == "visor") {
    if ((p = target()) < 0) return;
    String m = a.size() > 1 ? a[1] : "toggle";
    uint8_t v = m == "custom" ? PL_VISOR_CUSTOM : m == "rainbow" ? PL_VISOR_RAINBOW : PL_VISOR_TOGGLE;
    radio::command(p, PL_CMD_VISOR_MODE, &v, 1);
  } else if (c == "fx") {
    if ((p = target()) < 0) return;
    if (a.size() < 3) { print(CON_WARN, "fx <zone> <effect> [c1] [c2] [speed] [bri]"); return; }
    PlLedFx f = {};
    int z = zoneFrom(a[1]), e = fxFrom(a[2]);
    if (z < 0 || e < 0) { print(CON_ERR, "unknown zone or effect"); return; }
    f.zone = z;
    f.fx = e;
    f.r1 = 0x22; f.g1 = 0xE4; f.b1 = 0xFF;
    if (a.size() > 3 && !colorFrom(a[3], f.r1, f.g1, f.b1)) { print(CON_ERR, "colour: #RRGGBB or a name"); return; }
    if (a.size() > 4) colorFrom(a[4], f.r2, f.g2, f.b2);
    f.speed = a.size() > 5 ? parseLevel(a[5]) : 128;
    f.brightness = a.size() > 6 ? parseLevel(a[6]) : 0;
    sendFx(p, f, nullptr);
  } else if (c == "off" || c == "native") {
    if ((p = target()) < 0) return;
    PlLedFx f = {};
    f.zone = PL_ZONE_ALL;
    f.fx = c == "off" ? PL_FX_OFF : PL_FX_ANIM;
    f.speed = 128;
    sendFx(p, f, nullptr);
  } else if (c == "id" || c == "identify") {
    if ((p = target()) >= 0) radio::command(p, PL_CMD_IDENTIFY);
  } else if (c == "preset" || c == "presets") cmdPreset(a);
  else if (c == "say" || c == "raw") {
    if ((p = target()) < 0) return;
    int sp = line.indexOf(' ');
    if (sp < 0) { print(CON_WARN, "say <text>"); return; }
    radio::text(p, line.substring(sp + 1).c_str());
  } else if (c == "save") {
    if ((p = target()) >= 0) radio::command(p, PL_CMD_SAVE);
  } else if (c == "reboot") {
    if (a.size() > 1 && a[1] == "deck") { print(CON_WARN, "rebooting deck..."); delay(100); ESP.restart(); }
    if ((p = target()) >= 0) radio::command(p, PL_CMD_REBOOT);
  } else if (c == "ch" || c == "channel") {
    if (a.size() > 1) radio::setChannel(a[1].toInt());
    print(CON_TEXT, "radio channel %u", radio::channel());
  } else if (c == "bl" || c == "backlight") {
    if (a.size() > 1) {
      uint8_t v = constrain(a[1].toInt(), 5, 100);
      board::setBacklight(v);
      settings::get().backlight = v;
      settings::save();
    }
    print(CON_TEXT, "backlight %u%%", board::backlight());
  } else if (c == "sleep") board::sleep();
  else if (c == "beep") board::beep(60);
  else if (c == "clicks") {
    bool on = a.size() > 1 ? a[1] == "on" : !board::clicks();
    board::setClicks(on);
    settings::get().clicks = on;
    settings::save();
    print(CON_TEXT, "touch clicks %s", on ? "on" : "off");
  } else if (c == "ansi") {
    ansi = a.size() > 1 ? a[1] == "on" : !ansi;
    print(CON_TEXT, "ansi colours %s", ansi ? "on" : "off");
  } else if (c == "stats") {
    print(CON_TEXT, "tx %lu  rx %lu  lost %lu  ch %u  heap %lu KB  psram %lu KB", (unsigned long)radio::txCount(),
          (unsigned long)radio::rxCount(), (unsigned long)radio::lostCount(), radio::channel(),
          (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)(ESP.getFreePsram() / 1024));
  } else if (c == "history") {
    for (size_t i = 0; i < hist.size(); i++) print(CON_DIM, "%2u  %s", (unsigned)i, hist[i].c_str());
  } else if (c == "clear" || c == "cls") {
    if (clearFn) clearFn();
  } else if (c == "neofetch" || c == "about") neofetch();
  else print(CON_ERR, "unknown command '%s' - type help", c.c_str());
}

void onRadio(const RadioEvent &e) {
  Peer *p = radio::peer(e.peer);
  const char *nm = p ? p->hello.name : "?";
  switch (e.type) {
    case RadioEvt::ScanStart:
      print(CON_ACCENT, "sweeping %lu channel%s for ProtoLink nodes...", (unsigned long)e.value, e.value > 1 ? "s" : "");
      break;
    case RadioEvt::PeerFound:
      print(CON_OK, "+ %-19s %-10s ch %2u  %4d dBm  %s", nm, p->hello.kind, p->hello.channel, p->rssi,
            p->macStr().c_str());
      break;
    case RadioEvt::ScanDone:
      print(CON_ACCENT, "scan complete: %lu node%s online", (unsigned long)e.value, e.value == 1 ? "" : "s");
      break;
    case RadioEvt::Pong:
      if (e.seq == pingSeq) print(CON_OK, "pong from %s in %lu ms (%d dBm)", nm, (unsigned long)e.value, p->rssi);
      break;
    case RadioEvt::Telemetry:
      if (e.seq == teleSeq || (watchTele && e.peer == radio::selected())) printTele(*p);
      if (e.seq == teleSeq) teleSeq = -1;
      break;
    case RadioEvt::Anims:
      if (e.seq == animSeq || animSeq == -2) {
        print(CON_ACCENT, "%s: %u animations", nm, (unsigned)p->anims.size());
        String row;
        for (size_t i = 0; i < p->anims.size(); i++) {
          char cell[24];
          snprintf(cell, sizeof(cell), "%-19s", p->anims[i].c_str());
          row += cell;
          if (i % 4 == 3) { print(CON_TEXT, " %s", row.c_str()); row = ""; }
        }
        if (row.length()) print(CON_TEXT, " %s", row.c_str());
        animSeq = -1;
      }
      break;
    case RadioEvt::Ack:
      print(e.status == PL_OK ? CON_OK : CON_ERR, "%s: %s %s%s%s", nm, radio::cmdName(e.cmd),
            radio::statusName(e.status), e.text && e.text[0] ? " - " : "", e.text ? e.text : "");
      break;
    case RadioEvt::Text:
      print(CON_PINK, "%s> %s", nm, e.text);
      break;
    case RadioEvt::Log:
      print(CON_DIM, "[%s] %s", nm, e.text);
      break;
    case RadioEvt::Timeout:
      // background telemetry polls time out quietly
      if (e.cmd == PL_GET && e.status == PL_GET_TELEMETRY && e.seq != teleSeq) break;
      print(CON_WARN, "%s: no answer (%s)", nm, e.cmd == PL_CMD ? radio::cmdName(e.status) : "request");
      break;
    default:
      break;
  }
}

void begin() { Serial.setTimeout(5); }

void loop() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r' || ch == '\n') {
      if (serialLine.length()) exec(serialLine);
      serialLine = "";
    } else if (ch == 8 || ch == 127) {
      if (serialLine.length()) serialLine.remove(serialLine.length() - 1);
    } else if (serialLine.length() < 200) {
      serialLine += ch;
    }
  }
}

}  // namespace console
