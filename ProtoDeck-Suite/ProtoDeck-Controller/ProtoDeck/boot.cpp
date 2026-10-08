// ============================================================================
//  boot.cpp  -  the boot sequence
//   1. CRT power-on flash while the backlight fades in
//   2. BIOS-style report with REAL hardware info (chip, memory, I2C devices)
//   3. a random handful of flavour lines, interleaved with REAL init steps
//      (presets, radio bring-up, an actual channel sweep that lists nodes)
//   4. glitchy chromatic-aberration logo reveal, then fade to the main UI
//  Tap anywhere to fast-forward.
// ============================================================================
#include <functional>
#include <vector>
#include "board.h"
#include "config.h"
#include "presets.h"
#include "radio.h"
#include "ui.h"

namespace bootseq {
namespace {

inline lv_color_t hex(uint32_t c) { return lv_color_hex(c); }
const lv_font_t *MONO = &font_mono_14;   // 8 px per character
constexpr int COL_STATUS = 48;   // dots run up to this column
constexpr int MAX_ROWS = 21;

enum Kind : uint8_t { K_TYPE, K_PAUSE, K_COUNT, K_ACTION, K_SCAN, K_LOGO };

struct Step {
  Kind     kind;
  String   text;
  uint32_t color = C_TEXT;
  uint16_t dwell = 0;              // spinner time before the status appears
  String   status;                 // "" = plain line
  uint32_t statusColor = C_GREEN;
  String   note;
  std::function<void(Step &)> action;  // K_ACTION: fills status / note
  uint32_t countTo = 0;            // K_COUNT
};

struct Flavor { const char *text, *status; uint32_t color; const char *note; };
const Flavor FLAVOR[] = {
    {"Calibrating boop sensors", "OK", C_GREEN, ""},
    {"Reticulating visor splines", "OK", C_GREEN, ""},
    {"Polishing visor with ESD-safe cloth", "OK", C_GREEN, ""},
    {"Negotiating with the cooling fan", "OK", C_GREEN, "it agreed to whisper"},
    {"Synchronizing ear LEDs to vibes", "OK", C_GREEN, ""},
    {"Defragmenting fluff buffers", "WARN", C_AMBER, "lint quarantined"},
    {"Counting toe beans", "4/4", C_GREEN, ""},
    {"Checking snack reserves", "FAIL", C_RED, "proceeding anyway"},
    {"Compiling expressions  :3  >:3  owo", "OK", C_GREEN, ""},
    {"Engaging cuteness limiter", "WARN", C_AMBER, "limit exceeded"},
    {"Verifying floor is not lava", "SKIP", C_DIM, ""},
    {"Downloading more RAM", "FAIL", C_RED, "nice try"},
    {"Teaching the visor how to blink", "OK", C_GREEN, ""},
    {"Mounting /dev/tail", "OK", C_GREEN, ""},
    {"Patching cringe.dll", "OK", C_GREEN, "still cringe"},
    {"Untangling WS2812 data line", "OK", C_GREEN, ""},
    {"Recalibrating head-tilt confusion matrix", "OK", C_GREEN, ""},
    {"Overclocking whiskers", "SKIP", C_DIM, "no whiskers found"},
    {"Loading emotional subroutines", "OK", C_GREEN, "7 feelings"},
    {"Spinning up the 2.4 GHz scream chamber", "OK", C_GREEN, ""},
    {"Rolling for initiative", "NAT20", C_PINK, ""},
    {"Warming up mouth-flap actuators", "OK", C_GREEN, ""},
    {"Ensuring nobody touches the visor", "WARN", C_AMBER, "someone will"},
    {"Aligning chakras with the I2C bus", "OK", C_GREEN, ""},
    {"Re-reading the documentation", "SKIP", C_DIM, "real protos don't"},
    {"Feeding the watchdog", "OK", C_GREEN, "good boy"},
    {"Applying thermal paste to feelings", "OK", C_GREEN, ""},
    {"Searching for the any key", "WARN", C_AMBER, "not found, continuing"},
};
constexpr int N_FLAVOR = sizeof(FLAVOR) / sizeof(FLAVOR[0]);

std::vector<Step> steps;
size_t   idx = 0;
uint8_t  phase = 0;
uint32_t phaseAt = 0;
size_t   typed = 0;
bool     fast = false, active = false, radioUp = false;
int      foundDuringBoot = 0;
DoneFn   doneCb = nullptr;

lv_obj_t *scr = nullptr, *term = nullptr, *row = nullptr, *rowText = nullptr, *rowStat = nullptr, *crt = nullptr,
         *hint = nullptr;
lv_obj_t *logoMain = nullptr, *logoC = nullptr, *logoM = nullptr, *logoSub = nullptr, *logoBar = nullptr,
         *scanline = nullptr, *corner = nullptr;
lv_timer_t *tmr = nullptr;

// ---------------------------------------------------------------- rows
void newRow() {
  if (lv_obj_get_child_count(term) >= MAX_ROWS) lv_obj_delete(lv_obj_get_child(term, 0));
  row = lv_obj_create(term);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 760, 20);
  rowText = lv_label_create(row);
  lv_obj_set_style_text_font(rowText, MONO, 0);
  lv_label_set_text(rowText, "");
  rowStat = lv_label_create(row);
  lv_obj_set_style_text_font(rowStat, MONO, 0);
  lv_label_set_text(rowStat, "");
  lv_obj_set_pos(rowStat, COL_STATUS * 8, 0);
}

void printRow(const String &text, uint32_t color) {  // instant line (used for found nodes)
  newRow();
  lv_label_set_text(rowText, text.c_str());
  lv_obj_set_style_text_color(rowText, hex(color), 0);
}

String dotted(const String &t) {
  String s = t + " ";
  while ((int)s.length() < COL_STATUS - 1) s += '.';
  return s;
}

void showStatus(Step &s) {
  String st = s.status;
  while (st.length() < 4) st = " " + st + (st.length() < 3 ? " " : "");
  String full = "[" + st + "]";
  lv_label_set_text(rowStat, full.c_str());
  lv_obj_set_style_text_color(rowStat, hex(s.statusColor), 0);
  if (s.note.length()) {
    lv_obj_t *n = lv_label_create(row);
    lv_obj_set_style_text_font(n, MONO, 0);
    lv_obj_set_style_text_color(n, hex(C_DIM), 0);
    lv_label_set_text(n, s.note.c_str());
    lv_obj_set_pos(n, (COL_STATUS + full.length() + 1) * 8, 0);
  }
}

// ---------------------------------------------------------------- logo
void buildLogo() {
  lv_obj_add_flag(term, LV_OBJ_FLAG_HIDDEN);
  auto mk = [](uint32_t col, lv_opa_t opa) {
    lv_obj_t *l = lv_label_create(scr);
    lv_label_set_text(l, DECK_NAME);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(l, hex(col), 0);
    lv_obj_set_style_text_opa(l, opa, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, -30);
    return l;
  };
  logoC = mk(C_CYAN, LV_OPA_70);
  logoM = mk(C_PINK, LV_OPA_70);
  logoMain = mk(C_TEXT, LV_OPA_COVER);

  logoSub = lv_label_create(scr);
  lv_obj_set_style_text_font(logoSub, MONO, 0);
  lv_obj_set_style_text_color(logoSub, hex(C_CYAN), 0);
  lv_label_set_text(logoSub, "");
  lv_obj_align(logoSub, LV_ALIGN_CENTER, 0, 26);

  logoBar = lv_bar_create(scr);
  lv_obj_set_size(logoBar, 360, 4);
  lv_obj_align(logoBar, LV_ALIGN_CENTER, 0, 60);
  lv_bar_set_range(logoBar, 0, 100);
  lv_obj_set_style_radius(logoBar, 0, 0);
  lv_obj_set_style_radius(logoBar, 0, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(logoBar, hex(C_PANEL2), 0);
  lv_obj_set_style_bg_color(logoBar, hex(C_CYAN), LV_PART_INDICATOR);

  scanline = lv_obj_create(scr);
  lv_obj_remove_style_all(scanline);
  lv_obj_set_size(scanline, 800, 3);
  lv_obj_set_style_bg_color(scanline, hex(C_CYAN), 0);
  lv_obj_set_style_bg_opa(scanline, LV_OPA_30, 0);

  corner = lv_label_create(scr);
  lv_obj_set_style_text_font(corner, &font_mono_12, 0);
  lv_obj_set_style_text_color(corner, hex(C_DIM), 0);
  lv_label_set_text_fmt(corner, "BUILD " __DATE__ "  //  v" DECK_VERSION "  //  REV %u.%u", board::revision() / 10,
                        board::revision() % 10);
  lv_obj_align(corner, LV_ALIGN_BOTTOM_LEFT, 14, -12);
}

// returns true when the logo phase is finished
bool logoTick(uint32_t t) {
  const uint32_t GLITCH = fast ? 250 : 950, TYPE = fast ? 150 : 700, BAR = fast ? 200 : 750, HOLD = fast ? 100 : 450;
  if (t < GLITCH) {
    int r = esp_random();
    int dx = (r % 11) - 5, dy = ((r >> 8) % 5) - 2;
    lv_obj_align(logoC, LV_ALIGN_CENTER, dx, -30 + dy);
    lv_obj_align(logoM, LV_ALIGN_CENTER, -dx, -30 - dy);
    if (((r >> 16) % 7) == 0) lv_obj_add_flag(logoMain, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(logoMain, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(scanline, (t * 480 / GLITCH));
    return false;
  }
  lv_obj_remove_flag(logoMain, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(logoC, LV_ALIGN_CENTER, -2, -30);  // settle into a subtle RGB split
  lv_obj_align(logoM, LV_ALIGN_CENTER, 2, -30);
  lv_obj_add_flag(scanline, LV_OBJ_FLAG_HIDDEN);
  t -= GLITCH;
  static char sub[64];
  snprintf(sub, sizeof(sub), "neural link established  //  %d node%s in range", foundDuringBoot,
           foundDuringBoot == 1 ? "" : "s");
  size_t n = strlen(sub);
  if (t < TYPE) {
    size_t k = n * t / TYPE;
    char tmp[64];
    memcpy(tmp, sub, k);
    tmp[k] = '_';
    tmp[k + 1] = 0;
    lv_label_set_text(logoSub, tmp);
    return false;
  }
  lv_label_set_text(logoSub, sub);
  t -= TYPE;
  if (t < BAR) {
    lv_bar_set_value(logoBar, t * 100 / BAR, LV_ANIM_OFF);
    return false;
  }
  lv_bar_set_value(logoBar, 100, LV_ANIM_OFF);
  return t >= BAR + HOLD;
}

// ---------------------------------------------------------------- driver
void finish() {
  if (tmr) { lv_timer_delete(tmr); tmr = nullptr; }
  active = false;
  lv_obj_t *old = scr;
  scr = nullptr;
  if (doneCb) doneCb();
  if (old) lv_obj_delete_delayed(old, 700);
}

void tick(lv_timer_t *) {
  if (idx >= steps.size()) { finish(); return; }
  Step &s = steps[idx];
  uint32_t now = millis();
  uint32_t el = now - phaseAt;
  auto next = [&]() { idx++; phase = 0; phaseAt = now; typed = 0; };

  switch (s.kind) {
    case K_PAUSE:
      if (fast || el >= s.dwell) next();
      break;

    case K_TYPE:
    case K_ACTION:
    case K_COUNT:
    case K_SCAN: {
      if (phase == 0) {  // start a row
        newRow();
        lv_obj_set_style_text_color(rowText, hex(s.color), 0);
        phase = 1;
        phaseAt = now;
        typed = 0;
      }
      if (phase == 1) {  // typing
        String target = s.status.length() || s.kind != K_TYPE ? dotted(s.text) : s.text;
        typed = fast ? target.length() : typed + 3;
        if (typed > target.length()) typed = target.length();
        lv_label_set_text(rowText, target.substring(0, typed).c_str());
        if (typed >= target.length()) {
          if (s.kind == K_TYPE && !s.status.length()) { next(); break; }
          phase = 2;
          phaseAt = now;
          if (s.kind == K_SCAN) {
            if (radioUp && radio::startScan()) {
              foundDuringBoot = 0;
            } else {
              s.status = "SKIP";
              s.statusColor = C_DIM;
              s.note = "radio offline";
            }
          }
        }
        break;
      }
      if (phase == 2) {  // spinner / counter / waiting
        static const char spin[4] = {'|', '/', '-', '\\'};
        char sp[2] = {spin[(now / 90) % 4], 0};
        if (s.kind == K_COUNT) {
          uint32_t dur = fast ? 1 : s.dwell;
          uint32_t v = el >= dur ? s.countTo : (uint64_t)s.countTo * el / dur;
          lv_label_set_text_fmt(rowStat, "%lu KB", (unsigned long)v);
          lv_obj_set_style_text_color(rowStat, hex(C_TEXT), 0);
          if (el >= dur) { phase = 3; phaseAt = now; }
          break;
        }
        if (s.kind == K_SCAN && radio::scanning()) {
          lv_label_set_text_fmt(rowStat, "%s ch %02u", sp, radio::channel());
          lv_obj_set_style_text_color(rowStat, hex(C_CYAN), 0);
          break;
        }
        lv_label_set_text(rowStat, sp);
        lv_obj_set_style_text_color(rowStat, hex(C_CYAN), 0);
        if (fast || el >= s.dwell) { phase = 3; phaseAt = now; }
        break;
      }
      if (phase == 3) {  // resolve status
        if (s.kind == K_ACTION && s.action) s.action(s);
        if (s.kind == K_COUNT) { s.status = "OK"; s.statusColor = C_GREEN; }
        if (s.kind == K_SCAN && !s.status.length()) {
          int on = 0;
          for (int i = 0; i < radio::count(); i++)
            if (radio::peer(i)->online()) on++;
          foundDuringBoot = on;
          char b[12];
          snprintf(b, sizeof(b), "%d", on);
          s.status = String(b);
          s.statusColor = on ? C_GREEN : C_AMBER;
          s.note = on ? (on == 1 ? "node found" : "nodes found") : "nobody home (yet)";
          // the scan rows for found nodes were printed above this one; move this row to the end
          lv_obj_move_foreground(row);
        }
        if (s.kind == K_COUNT) {
          // keep the KB counter visible, append status after it
          lv_obj_t *st = lv_label_create(row);
          lv_obj_set_style_text_font(st, MONO, 0);
          lv_obj_set_style_text_color(st, hex(C_GREEN), 0);
          lv_label_set_text(st, "[ OK ]");
          lv_obj_set_pos(st, (COL_STATUS + 10) * 8, 0);
        } else {
          showStatus(s);
        }
        next();
      }
      break;
    }

    case K_LOGO:
      if (phase == 0) {
        buildLogo();
        phase = 1;
        phaseAt = now;
      }
      if (logoTick(el)) next();
      break;
  }
}

void add(Kind k, const String &text, uint32_t color = C_TEXT, uint16_t dwell = 0) {
  Step s;
  s.kind = k;
  s.text = text;
  s.color = color;
  s.dwell = dwell;
  steps.push_back(s);
}
void addAction(const String &text, uint16_t dwell, std::function<void(Step &)> fn) {
  Step s;
  s.kind = K_ACTION;
  s.text = text;
  s.dwell = dwell;
  s.action = fn;
  steps.push_back(s);
}
void addFlavor(const Flavor &f) {
  Step s;
  s.kind = K_ACTION;
  s.text = f.text;
  s.dwell = 100 + esp_random() % 260;
  String st = f.status, note = f.note;
  uint32_t col = f.color;
  s.action = [st, note, col](Step &me) { me.status = st; me.note = note; me.statusColor = col; };
  steps.push_back(s);
}

void buildSteps() {
  steps.clear();
  char b[96];
  int pool[N_FLAVOR];
  for (int i = 0; i < N_FLAVOR; i++) pool[i] = i;
  for (int i = N_FLAVOR - 1; i > 0; i--) {  // shuffle: every boot is a little different
    int j = esp_random() % (i + 1);
    int t = pool[i]; pool[i] = pool[j]; pool[j] = t;
  }
  int fl = 0;

  add(K_PAUSE, "", C_TEXT, 420);  // CRT flash
  snprintf(b, sizeof(b), "%s BIOS v%s   //   (c) the protogen collective", DECK_NAME, DECK_VERSION);
  add(K_TYPE, b, C_CYAN);
  snprintf(b, sizeof(b), "CPU   %s rev %d.%d   2x Xtensa LX7 @ %lu MHz", ESP.getChipModel(), ESP.getChipRevision() / 100,
           ESP.getChipRevision() % 100, (unsigned long)ESP.getCpuFreqMHz());
  add(K_TYPE, b, C_TEXT);
  snprintf(b, sizeof(b), "MEM   %lu KB SRAM free   %lu KB PSRAM   %lu MB FLASH", (unsigned long)(ESP.getFreeHeap() / 1024),
           (unsigned long)(ESP.getPsramSize() / 1024), (unsigned long)(ESP.getFlashChipSize() >> 20));
  add(K_TYPE, b, C_TEXT);
  snprintf(b, sizeof(b), "BOARD CrowPanel Advance 7.0\" HMI   rev %u.%u", board::revision() / 10, board::revision() % 10);
  add(K_TYPE, b, C_TEXT);
  {
    Step s;
    s.kind = K_COUNT;
    s.text = "Testing PSRAM";
    s.dwell = 700;
    s.countTo = ESP.getPsramSize() / 1024;
    steps.push_back(s);
  }
  add(K_TYPE, "", C_TEXT);

  addAction("Probing I2C bus (SDA 15 / SCL 16)", 220, [](Step &s) {
    const board::ProbeReport &p = board::probe();
    String n;
    if (p.helperMcu) n += "30:STC8 ";
    if (p.ioExpander) n += "18:IOEX ";
    if (p.touch) n += String(p.touchAddr, HEX) + ":GT911 ";
    if (p.rtc) n += "51:RTC";
    s.status = "OK";
    s.statusColor = C_GREEN;
    s.note = n;
  });
  snprintf(b, sizeof(b), "RGB panel 800x480 @ %u MHz, bounce buffers", (unsigned)(LCD_PCLK_HZ / 1000000));
  addAction(b, 160, [](Step &s) { s.status = "OK"; s.statusColor = C_GREEN; });
  addAction("Capacitive touch (GT911)", 200, [](Step &s) {
    bool ok = board::probe().touch;
    s.status = ok ? "OK" : "FAIL";
    s.statusColor = ok ? C_GREEN : C_RED;
    s.note = ok ? "" : "check BOARD_REV in config.h";
  });
  addFlavor(FLAVOR[pool[fl++]]);
  addFlavor(FLAVOR[pool[fl++]]);
  addAction("Loading LED presets from flash", 180, [](Step &s) {
    s.status = "OK";
    s.statusColor = C_GREEN;
    s.note = String(presets::count()) + " slots";
  });
  addFlavor(FLAVOR[pool[fl++]]);
  addAction("Bringing up 2.4 GHz radio (ESP-NOW)", 300, [](Step &s) {
    if (!radioUp) {
      radioUp = radio::begin();
      if (radioUp) radio::setChannel(settings::get().homeChannel);
    }
    s.status = radioUp ? "OK" : "FAIL";
    s.statusColor = radioUp ? C_GREEN : C_RED;
    s.note = radioUp ? radio::myMac() : "esp_now_init failed";
  });
  addFlavor(FLAVOR[pool[fl++]]);
  if (settings::get().scanOnBoot) {
    Step s;
    s.kind = K_SCAN;
    s.text = "Sweeping channels 1-13 for kin";
    s.color = C_TEXT;
    steps.push_back(s);
  }
  addFlavor(FLAVOR[pool[fl++]]);
  addFlavor(FLAVOR[pool[fl++]]);
  add(K_TYPE, "", C_TEXT);
  add(K_TYPE, "All systems nominal. Probably.", C_GREEN);
  add(K_PAUSE, "", C_TEXT, 550);
  Step logo;
  logo.kind = K_LOGO;
  steps.push_back(logo);
}

void crtFlash() {
  crt = lv_obj_create(scr);
  lv_obj_remove_style_all(crt);
  lv_obj_set_style_bg_color(crt, hex(0xE8FBFF), 0);
  lv_obj_set_style_bg_opa(crt, LV_OPA_COVER, 0);
  lv_obj_set_size(crt, 0, 2);
  lv_obj_center(crt);

  lv_anim_t a;
  lv_anim_init(&a);  // horizontal line grows
  lv_anim_set_var(&a, crt);
  lv_anim_set_values(&a, 0, 800);
  lv_anim_set_duration(&a, 140);
  lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_obj_set_width((lv_obj_t *)o, v); lv_obj_center((lv_obj_t *)o); });
  lv_anim_start(&a);

  lv_anim_t b;
  lv_anim_init(&b);  // ...then opens vertically and fades
  lv_anim_set_var(&b, crt);
  lv_anim_set_values(&b, 2, 480);
  lv_anim_set_delay(&b, 140);
  lv_anim_set_duration(&b, 200);
  lv_anim_set_path_cb(&b, lv_anim_path_ease_in);
  lv_anim_set_exec_cb(&b, [](void *o, int32_t v) {
    lv_obj_set_height((lv_obj_t *)o, v);
    lv_obj_center((lv_obj_t *)o);
    lv_obj_set_style_bg_opa((lv_obj_t *)o, 255 - v * 255 / 480, 0);
  });
  lv_anim_set_completed_cb(&b, [](lv_anim_t *an) { lv_obj_add_flag((lv_obj_t *)an->var, LV_OBJ_FLAG_HIDDEN); });
  lv_anim_start(&b);

  lv_anim_t bl;
  lv_anim_init(&bl);  // backlight fade-in
  lv_anim_set_var(&bl, scr);
  lv_anim_set_values(&bl, 0, settings::get().backlight);
  lv_anim_set_duration(&bl, 450);
  lv_anim_set_exec_cb(&bl, [](void *, int32_t v) { board::setBacklight(v); });
  lv_anim_start(&bl);
}

}  // namespace

void start(DoneFn done) {
  if (active) return;
  doneCb = done;
  active = true;
  fast = false;
  idx = 0;
  phase = 0;
  phaseAt = millis();
  typed = 0;
  foundDuringBoot = 0;
  board::setBacklight(0);

  scr = lv_obj_create(nullptr);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(scr, hex(0x000000), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(scr, [](lv_event_t *) { fast = true; }, LV_EVENT_CLICKED, nullptr);

  term = lv_obj_create(scr);
  lv_obj_remove_style_all(term);
  lv_obj_set_size(term, 780, 460);
  lv_obj_set_pos(term, 20, 14);
  lv_obj_set_flex_flow(term, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(term, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(term, LV_OBJ_FLAG_CLICKABLE);

  hint = lv_label_create(scr);
  lv_obj_set_style_text_font(hint, &font_mono_12, 0);
  lv_obj_set_style_text_color(hint, hex(0x2A3C48), 0);
  lv_label_set_text(hint, "TAP TO SKIP");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_RIGHT, -14, -10);

  lv_screen_load(scr);
  buildSteps();
  crtFlash();
  tmr = lv_timer_create(tick, 16, nullptr);
}

bool running() { return active; }

void onRadio(const RadioEvent &e) {
  if (!active || !term || e.type != RadioEvt::PeerFound) return;
  Peer *p = radio::peer(e.peer);
  if (!p) return;
  char b[96];
  snprintf(b, sizeof(b), "   >> %-18s %-10s ch %02u  %d dBm", p->hello.name, p->hello.kind, p->hello.channel, p->rssi);
  lv_obj_t *keep = row;  // the "Sweeping..." row stays current
  lv_obj_t *kt = rowText, *ks = rowStat;
  printRow(b, C_GREEN);
  row = keep;
  rowText = kt;
  rowStat = ks;
}

}  // namespace bootseq
