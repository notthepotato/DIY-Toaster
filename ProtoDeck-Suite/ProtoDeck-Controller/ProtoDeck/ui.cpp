// ============================================================================
//  ui.cpp  -  ProtoDeck main interface
//  Layout (800x480):  top status bar 36px | nav rail 96px | page 704x444
//  Pages: NODES  CONTROL  POWER  FX  TERM  SYS
// ============================================================================
#include "ui.h"
#include "board.h"
#include "config.h"
#include "console.h"
#include "pl_effects.h"
#include "presets.h"
#include <math.h>

namespace ui {
namespace {

// ------------------------------------------------------------------ basics
inline lv_color_t hex(uint32_t c) { return lv_color_hex(c); }
const lv_font_t *F12 = &lv_font_montserrat_12, *F14 = &lv_font_montserrat_14, *F16 = &lv_font_montserrat_16,
                *F20 = &lv_font_montserrat_20, *F24 = &lv_font_montserrat_24,
                *M8 = &font_mono_12, *M16 = &font_mono_14, *M16B = &font_mono_16, *MV = &font_mono_30;

enum Page { P_NODES, P_CTRL, P_POWER, P_FX, P_TERM, P_SYS, P_COUNT };

lv_style_t stCard, stBtn, stBtnPr, stBtnChk, stBtnDis;
lv_obj_t *scr = nullptr, *content = nullptr, *kb = nullptr, *toastBox = nullptr, *toastLbl = nullptr;
lv_obj_t *pages[P_COUNT], *navBtns[P_COUNT];
int curPage = P_NODES;
bool built = false;

void initStyles() {
  lv_style_init(&stCard);
  lv_style_set_bg_color(&stCard, hex(C_PANEL));
  lv_style_set_bg_opa(&stCard, LV_OPA_COVER);
  lv_style_set_border_color(&stCard, hex(C_LINE));
  lv_style_set_border_width(&stCard, 1);
  lv_style_set_radius(&stCard, 4);
  lv_style_set_pad_all(&stCard, 10);
  lv_style_set_shadow_width(&stCard, 0);

  lv_style_init(&stBtn);
  lv_style_set_bg_color(&stBtn, hex(C_PANEL2));
  lv_style_set_bg_opa(&stBtn, LV_OPA_COVER);
  lv_style_set_border_color(&stBtn, hex(C_LINE));
  lv_style_set_border_width(&stBtn, 1);
  lv_style_set_radius(&stBtn, 4);
  lv_style_set_text_color(&stBtn, hex(C_TEXT));
  lv_style_set_text_font(&stBtn, F14);
  lv_style_set_pad_all(&stBtn, 4);

  lv_style_init(&stBtnPr);
  lv_style_set_bg_color(&stBtnPr, hex(C_CYAN));
  lv_style_set_bg_opa(&stBtnPr, LV_OPA_40);
  lv_style_set_border_color(&stBtnPr, hex(C_CYAN));

  lv_style_init(&stBtnChk);
  lv_style_set_bg_color(&stBtnChk, hex(C_CYAN));
  lv_style_set_bg_opa(&stBtnChk, LV_OPA_COVER);
  lv_style_set_border_color(&stBtnChk, hex(C_CYAN));
  lv_style_set_text_color(&stBtnChk, hex(C_BG));

  lv_style_init(&stBtnDis);
  lv_style_set_opa(&stBtnDis, LV_OPA_30);
}

lv_obj_t *box(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t *label(lv_obj_t *parent, const char *txt, const lv_font_t *f, uint32_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, hex(color), 0);
  return l;
}

lv_obj_t *card(lv_obj_t *parent, int w, int h) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_add_style(c, &stCard, 0);
  lv_obj_set_size(c, w, h);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

// little L-shaped corner marks - the "HUD" look
void brackets(lv_obj_t *o, uint32_t color, int len = 10) {
  int off = -(lv_obj_get_style_pad_left(o, 0) + lv_obj_get_style_border_width(o, 0));
  const lv_align_t al[4] = {LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_RIGHT, LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_BOTTOM_RIGHT};
  const int sx[4] = {1, -1, 1, -1}, sy[4] = {1, 1, -1, -1};
  for (int i = 0; i < 4; i++) {
    for (int k = 0; k < 2; k++) {
      lv_obj_t *b = box(o);
      lv_obj_add_flag(b, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_set_style_bg_color(b, hex(color), 0);
      lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
      lv_obj_set_size(b, k ? 2 : len, k ? len : 2);
      lv_obj_align(b, al[i], sx[i] * off, sy[i] * off);
    }
  }
}

lv_obj_t *button(lv_obj_t *parent, const char *txt, int w, int h, lv_event_cb_t cb, void *ud = nullptr,
                 uint32_t accent = 0) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_add_style(b, &stBtn, 0);
  lv_obj_add_style(b, &stBtnPr, LV_STATE_PRESSED);
  lv_obj_add_style(b, &stBtnChk, LV_STATE_CHECKED);
  lv_obj_add_style(b, &stBtnDis, LV_STATE_DISABLED);
  if (accent) {
    lv_obj_set_style_border_color(b, hex(accent), 0);
    lv_obj_set_style_text_color(b, hex(accent), 0);
  }
  lv_obj_set_size(b, w, h);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}

lv_obj_t *slider(lv_obj_t *parent, int w, int32_t mn, int32_t mx, uint32_t color = C_CYAN) {
  lv_obj_t *s = lv_slider_create(parent);
  lv_obj_set_width(s, w);
  lv_obj_set_height(s, 8);
  lv_slider_set_range(s, mn, mx);
  lv_obj_set_style_bg_color(s, hex(C_PANEL2), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s, hex(color), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s, hex(color), LV_PART_KNOB);
  lv_obj_set_style_pad_all(s, 5, LV_PART_KNOB);
  lv_obj_set_ext_click_area(s, 12);
  return s;
}

lv_obj_t *chip(lv_obj_t *parent, const char *txt, uint32_t color, bool filled = false) {
  lv_obj_t *c = box(parent);
  lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(c, 5, 0);
  lv_obj_set_style_pad_ver(c, 2, 0);
  lv_obj_set_style_radius(c, 3, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_border_color(c, hex(color), 0);
  if (filled) {
    lv_obj_set_style_bg_color(c, hex(color), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  }
  label(c, txt, M8, filled ? C_BG : color);
  return c;
}

void signalBars(lv_obj_t **bars, lv_obj_t *parent, int x, int y) {
  for (int i = 0; i < 4; i++) {
    bars[i] = box(parent);
    lv_obj_set_size(bars[i], 4, 4 + i * 3);
    lv_obj_set_style_bg_opa(bars[i], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bars[i], hex(C_LINE), 0);
    lv_obj_set_pos(bars[i], x + i * 6, y + (9 - i * 3));
  }
}
void setBars(lv_obj_t **bars, int rssi, bool online) {
  int lvl = !online ? 0 : rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1;
  uint32_t col = lvl >= 3 ? C_GREEN : lvl == 2 ? C_AMBER : C_RED;
  for (int i = 0; i < 4; i++) lv_obj_set_style_bg_color(bars[i], hex(i < lvl ? col : C_LINE), 0);
}

// ------------------------------------------------------------------ top bar
lv_obj_t *tbCursor, *tbDot, *tbTarget, *tbBars[4], *tbRssi, *tbCh, *tbNodes, *tbClock;

void buildTopBar() {
  lv_obj_t *top = box(scr);
  lv_obj_set_size(top, 800, 36);
  lv_obj_set_style_bg_color(top, hex(C_PANEL), 0);
  lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(top, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(top, 1, 0);
  lv_obj_set_style_border_color(top, hex(C_LINE), 0);

  lv_obj_t *logo = label(top, DECK_NAME, F20, C_CYAN);
  lv_obj_align(logo, LV_ALIGN_LEFT_MID, 14, 0);
  tbCursor = label(top, "\xE2\x96\x8C", M16B, C_PINK);  // U+258C block cursor
  lv_obj_align_to(tbCursor, logo, LV_ALIGN_OUT_RIGHT_MID, 2, 0);

  tbDot = box(top);
  lv_obj_set_size(tbDot, 10, 10);
  lv_obj_set_style_radius(tbDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(tbDot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(tbDot, hex(C_RED), 0);
  lv_obj_align(tbDot, LV_ALIGN_LEFT_MID, 214, 0);
  tbTarget = label(top, "NO TARGET", F16, C_DIM);
  lv_obj_align(tbTarget, LV_ALIGN_LEFT_MID, 232, 0);
  signalBars(tbBars, top, 420, 11);
  tbRssi = label(top, "", M8, C_DIM);
  lv_obj_align(tbRssi, LV_ALIGN_LEFT_MID, 450, 1);

  tbClock = label(top, "00:00:00", M16B, C_TEXT);
  lv_obj_align(tbClock, LV_ALIGN_RIGHT_MID, -14, 0);
  tbNodes = label(top, "0 NODES", M8, C_DIM);
  lv_obj_align(tbNodes, LV_ALIGN_RIGHT_MID, -100, 0);
  tbCh = label(top, "CH 01", M8, C_PINK);
  lv_obj_align(tbCh, LV_ALIGN_RIGHT_MID, -180, 0);
}

void refreshTopBar() {
  static bool blink = false;
  blink = !blink;
  if (blink) lv_obj_remove_flag(tbCursor, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(tbCursor, LV_OBJ_FLAG_HIDDEN);

  Peer *p = radio::selectedPeer();
  if (p) {
    lv_label_set_text(tbTarget, p->hello.name);
    lv_obj_set_style_text_color(tbTarget, hex(p->online() ? C_TEXT : C_DIM), 0);
    lv_obj_set_style_bg_color(tbDot, hex(p->online() ? C_GREEN : C_RED), 0);
    setBars(tbBars, p->rssi, p->online());
    lv_label_set_text_fmt(tbRssi, "%d dBm", p->rssi);
  } else {
    lv_label_set_text(tbTarget, "NO TARGET");
    lv_obj_set_style_text_color(tbTarget, hex(C_DIM), 0);
    lv_obj_set_style_bg_color(tbDot, hex(C_RED), 0);
    setBars(tbBars, -100, false);
    lv_label_set_text(tbRssi, "");
  }
  int on = 0;
  for (int i = 0; i < radio::count(); i++)
    if (radio::peer(i)->online()) on++;
  lv_label_set_text_fmt(tbNodes, "%d/%d NODES", on, radio::count());
  lv_label_set_text_fmt(tbCh, radio::scanning() ? "CH %02u SCAN" : "CH %02u", radio::channel());
  uint32_t s = millis() / 1000;
  lv_label_set_text_fmt(tbClock, "%02lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
                        (unsigned long)(s % 60));
}

// ------------------------------------------------------------------ nav
void showPage(int i);

void buildNav() {
  lv_obj_t *nav = box(scr);
  lv_obj_set_pos(nav, 0, 36);
  lv_obj_set_size(nav, 96, 444);
  lv_obj_set_style_bg_color(nav, hex(C_PANEL), 0);
  lv_obj_set_style_bg_opa(nav, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_width(nav, 1, 0);
  lv_obj_set_style_border_color(nav, hex(C_LINE), 0);

  static const char *sym[P_COUNT] = {LV_SYMBOL_WIFI, LV_SYMBOL_PLAY, LV_SYMBOL_CHARGE,
                                     LV_SYMBOL_TINT, LV_SYMBOL_KEYBOARD, LV_SYMBOL_SETTINGS};
  static const char *txt[P_COUNT] = {"NODES", "CONTROL", "POWER", "FX", "TERM", "SYS"};
  for (int i = 0; i < P_COUNT; i++) {
    lv_obj_t *b = lv_button_create(nav);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 95, 74);
    lv_obj_set_pos(b, 0, i * 74);
    lv_obj_set_style_text_color(b, hex(C_DIM), 0);
    lv_obj_set_style_bg_color(b, hex(C_PANEL2), LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_LEFT, LV_STATE_CHECKED);
    lv_obj_set_style_border_width(b, 3, LV_STATE_CHECKED);
    lv_obj_set_style_border_color(b, hex(C_CYAN), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(b, hex(C_CYAN), LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(b, hex(C_PANEL2), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_t *ic = lv_label_create(b);
    lv_label_set_text(ic, sym[i]);
    lv_obj_set_style_text_font(ic, F24, 0);
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_t *tl = lv_label_create(b);
    lv_label_set_text(tl, txt[i]);
    lv_obj_set_style_text_font(tl, F12, 0);
    lv_obj_align(tl, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(
        b, [](lv_event_t *e) { showPage((int)(intptr_t)lv_event_get_user_data(e)); }, LV_EVENT_CLICKED,
        (void *)(intptr_t)i);
    navBtns[i] = b;
  }
}

lv_obj_t *makePage() {
  lv_obj_t *p = box(content);
  lv_obj_set_size(p, 704, 444);
  lv_obj_set_style_pad_all(p, 14, 0);
  lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
  return p;
}

lv_obj_t *pageHeader(lv_obj_t *page, const char *title, const char *sub, lv_obj_t **subOut = nullptr) {
  lv_obj_t *t = label(page, title, F24, C_TEXT);
  lv_obj_set_pos(t, 0, -4);
  lv_obj_t *s = label(page, sub, M8, C_DIM);
  lv_obj_set_pos(s, 2, 26);
  if (subOut) *subOut = s;
  return t;
}

// ------------------------------------------------------------------ keyboard
extern lv_obj_t *termIn;
extern bool termDirty;
void kbHide() {
  if (!kb) return;
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_height(pages[P_TERM], 444);
  lv_obj_t *ta = lv_keyboard_get_textarea(kb);
  if (ta) lv_obj_remove_state(ta, LV_STATE_FOCUSED);
  lv_keyboard_set_textarea(kb, nullptr);
}
void kbAttach(lv_obj_t *ta) {
  lv_obj_add_event_cb(
      ta,
      [](lv_event_t *e) {
        lv_obj_t *t = lv_event_get_target_obj(e);
        lv_event_code_t c = lv_event_get_code(e);
        if (c == LV_EVENT_FOCUSED || c == LV_EVENT_CLICKED) {
          lv_keyboard_set_textarea(kb, t);
          lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
          lv_obj_move_foreground(kb);
          if (curPage == P_TERM) {  // squeeze the terminal so the input row sits above the keys
            lv_obj_set_height(pages[P_TERM], 444 - 220);
            termDirty = true;
          }
        } else if (c == LV_EVENT_CANCEL || c == LV_EVENT_DEFOCUSED) {
          kbHide();
        }
      },
      LV_EVENT_ALL, nullptr);
}

// ================================================================== NODES
struct NodeCard {
  lv_obj_t *card, *name, *info, *caps, *status, *dot, *tag, *bars[4];
};
NodeCard cards[LINK_MAX_PEERS];
int nCards = -1;
lv_obj_t *nodeList, *nodeEmpty, *scanBtn, *scanBar, *scanLbl;
uint32_t lastScanEnd = 0;

String capsStr(uint32_t c) {
  String s;
  if (c & PL_CAP_ANIMS) s += "ANIM  ";
  if (c & PL_CAP_LEDFX) s += "LED-FX  ";
  if (c & PL_CAP_POWER) s += "POWER  ";
  if (c & PL_CAP_FAN) s += "FAN  ";
  if (c & PL_CAP_MIC) s += "MIC  ";
  if (c & PL_CAP_TEXT) s += "TERM";
  return s;
}

void nodesRefresh() {
  for (int i = 0; i < nCards && i < radio::count(); i++) {
    Peer *p = radio::peer(i);
    NodeCard &c = cards[i];
    bool sel = i == radio::selected();
    lv_obj_set_style_border_color(c.card, hex(sel ? C_CYAN : C_LINE), 0);
    lv_obj_set_style_border_width(c.card, sel ? 2 : 1, 0);
    if (sel) lv_obj_remove_flag(c.tag, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(c.tag, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(c.info, "CH %02u   %d dBm   %lu ms   %s", p->hello.channel, p->rssi,
                          (unsigned long)p->rttMs, p->macStr().substring(9).c_str());
    bool on = p->online();
    lv_obj_set_style_bg_color(c.dot, hex(on ? C_GREEN : C_RED), 0);
    uint32_t ago = (millis() - p->lastSeen) / 1000;
    if (on) lv_label_set_text(c.status, "ONLINE");
    else lv_label_set_text_fmt(c.status, "LOST %lus AGO", (unsigned long)ago);
    lv_obj_set_style_text_color(c.status, hex(on ? C_GREEN : C_DIM), 0);
    setBars(c.bars, p->rssi, on);
  }
}

void nodesRebuild() {
  lv_obj_clean(nodeList);
  nCards = radio::count();
  if (nCards == 0) lv_obj_remove_flag(nodeEmpty, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(nodeEmpty, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < nCards; i++) {
    Peer *p = radio::peer(i);
    NodeCard &c = cards[i];
    c.card = card(nodeList, 331, 108);
    lv_obj_add_flag(c.card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(c.card, hex(C_PANEL2), LV_STATE_PRESSED);
    lv_obj_add_event_cb(
        c.card,
        [](lv_event_t *e) {
          int idx = (int)(intptr_t)lv_event_get_user_data(e);
          radio::select(idx);
          Peer *pp = radio::peer(idx);
          char msg[48];
          snprintf(msg, sizeof(msg), "TARGET LOCKED: %s", pp ? pp->hello.name : "?");
          toast(msg, C_CYAN);
          console::print(CON_OK, "target -> %s", pp ? pp->hello.name : "?");
          nodesRefresh();
        },
        LV_EVENT_CLICKED, (void *)(intptr_t)i);

    c.name = label(c.card, p->hello.name, F20, C_TEXT);
    lv_obj_set_pos(c.name, 0, -2);
    lv_obj_t *k = chip(c.card, p->hello.kind, C_PINK);
    lv_obj_align(k, LV_ALIGN_TOP_RIGHT, 0, 0);
    c.info = label(c.card, "", M8, C_DIM);
    lv_obj_set_pos(c.info, 0, 28);
    c.caps = label(c.card, capsStr(p->hello.caps).c_str(), M8, C_CYAN);
    lv_obj_set_pos(c.caps, 0, 44);
    c.dot = box(c.card);
    lv_obj_set_size(c.dot, 8, 8);
    lv_obj_set_style_radius(c.dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(c.dot, LV_OPA_COVER, 0);
    lv_obj_set_pos(c.dot, 0, 73);
    c.status = label(c.card, "", M8, C_GREEN);
    lv_obj_set_pos(c.status, 14, 68);
    c.tag = chip(c.card, "TARGET", C_CYAN, true);
    lv_obj_align(c.tag, LV_ALIGN_BOTTOM_RIGHT, -34, 2);
    signalBars(c.bars, c.card, 285, 62);
  }
  nodesRefresh();
}

void buildNodes() {
  lv_obj_t *pg = pages[P_NODES] = makePage();
  pageHeader(pg, "NEARBY NODES", "PROTOLINK  //  ESP-NOW  //  CH 1-13");
  scanBtn = button(
      pg, LV_SYMBOL_REFRESH "  SCAN", 150, 44,
      [](lv_event_t *) { console::exec("scan"); }, nullptr, C_CYAN);
  lv_obj_align(scanBtn, LV_ALIGN_TOP_RIGHT, 0, -6);

  scanBar = lv_bar_create(pg);
  lv_obj_set_size(scanBar, 676, 4);
  lv_obj_set_pos(scanBar, 0, 46);
  lv_bar_set_range(scanBar, 0, 100);
  lv_obj_set_style_radius(scanBar, 0, 0);
  lv_obj_set_style_radius(scanBar, 0, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(scanBar, hex(C_PANEL2), 0);
  lv_obj_set_style_bg_color(scanBar, hex(C_CYAN), LV_PART_INDICATOR);
  scanLbl = label(pg, "IDLE  //  NO SWEEP YET", M8, C_DIM);
  lv_obj_set_pos(scanLbl, 0, 56);

  nodeList = box(pg);
  lv_obj_set_pos(nodeList, 0, 74);
  lv_obj_set_size(nodeList, 676, 342);
  lv_obj_add_flag(nodeList, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(nodeList, LV_DIR_VER);
  lv_obj_set_flex_flow(nodeList, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(nodeList, 12, 0);

  nodeEmpty = box(pg);
  lv_obj_set_size(nodeEmpty, 676, 200);
  lv_obj_set_pos(nodeEmpty, 0, 140);
  lv_obj_t *ic = label(nodeEmpty, LV_SYMBOL_WIFI, &lv_font_montserrat_48, C_LINE);
  lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_t *t1 = label(nodeEmpty, "NO SIGNALS", F20, C_DIM);
  lv_obj_align(t1, LV_ALIGN_TOP_MID, 0, 70);
  lv_obj_t *t2 = label(nodeEmpty, "tap SCAN to sweep channels 1-13 for ProtoLink nodes", M8, C_DIM);
  lv_obj_align(t2, LV_ALIGN_TOP_MID, 0, 102);
  nodesRebuild();
}

// ================================================================== CONTROL
lv_obj_t *ctlSub, *animGrid, *animTitle, *ctlNoTarget, *sVisor, *sEars, *sFan, *vVisor, *vEars, *vFan, *modeBm;
int animsShownFor = -2;
size_t animsShownCount = 0;
String animsShownCurrent;
uint32_t lastSliderSend = 0;

void animButtonsSync() {
  Peer *p = radio::selectedPeer();
  String cur = p && p->hasTele ? String(p->tele.anim) : "";
  animsShownCurrent = cur;
  uint32_t n = lv_obj_get_child_count(animGrid);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *b = lv_obj_get_child(animGrid, i);
    size_t k = (size_t)(intptr_t)lv_obj_get_user_data(b);  // label text may be "..."-truncated
    if (p && cur.length() && k < p->anims.size() && cur.equalsIgnoreCase(p->anims[k]))
      lv_obj_add_state(b, LV_STATE_CHECKED);
    else
      lv_obj_remove_state(b, LV_STATE_CHECKED);
  }
}

void animsRebuild() {
  lv_obj_clean(animGrid);
  Peer *p = radio::selectedPeer();
  animsShownFor = radio::selected();
  animsShownCount = p ? p->anims.size() : 0;
  if (!p) return;
  if (!p->has(PL_CAP_ANIMS)) {
    lv_label_set_text(animTitle, "ANIMATIONS  //  NOT SUPPORTED BY THIS NODE");
    return;
  }
  lv_label_set_text_fmt(animTitle, "ANIMATIONS  //  %u", (unsigned)p->anims.size());
  for (size_t k = 0; k < p->anims.size(); k++) {
    lv_obj_t *b = button(
        animGrid, p->anims[k].c_str(), 124, 44,
        [](lv_event_t *e) {
          Peer *pp = radio::selectedPeer();
          size_t idx = (size_t)(intptr_t)lv_event_get_user_data(e);
          if (pp && idx < pp->anims.size()) console::exec(String("anim ") + pp->anims[idx]);
        },
        (void *)(intptr_t)k);
    lv_obj_set_user_data(b, (void *)(intptr_t)k);
    lv_obj_t *l = lv_obj_get_child(b, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, 116);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  }
  animButtonsSync();
}

void sliderEvent(lv_event_t *e) {
  lv_obj_t *s = lv_event_get_target_obj(e);
  int p = radio::selected();
  if (p < 0) return;
  int v = lv_slider_get_value(s);
  bool released = lv_event_get_code(e) == LV_EVENT_RELEASED;
  lv_obj_t *vl = s == sVisor ? vVisor : s == sEars ? vEars : vFan;
  lv_label_set_text_fmt(vl, "%d", v);
  if (!released && millis() - lastSliderSend < 90) return;
  lastSliderSend = millis();
  if (s == sFan) {
    uint8_t d = v;
    radio::command(p, PL_CMD_FAN, &d, 1);
  } else {
    radio::brightness(p, s == sVisor ? PL_ZONE_VISOR : PL_ZONE_EARS, v);
  }
  if (released) console::print(CON_DIM, "-> %s %d", s == sFan ? "fan" : s == sVisor ? "bright visor" : "bright ears", v);
}

lv_obj_t *sliderRow(lv_obj_t *parent, const char *name, int y, lv_obj_t **valOut, uint32_t color) {
  lv_obj_t *n = label(parent, name, M8, C_DIM);
  lv_obj_set_pos(n, 0, y);
  *valOut = label(parent, "--", M8, color);
  lv_obj_align(*valOut, LV_ALIGN_TOP_RIGHT, 0, y);
  lv_obj_t *s = slider(parent, 214, 0, 255, color);
  lv_obj_set_pos(s, 2, y + 18);
  lv_obj_add_event_cb(s, sliderEvent, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(s, sliderEvent, LV_EVENT_RELEASED, nullptr);
  return s;
}

void buildControl() {
  lv_obj_t *pg = pages[P_CTRL] = makePage();
  pageHeader(pg, "CONTROL", "--", &ctlSub);

  lv_obj_t *prev = button(pg, LV_SYMBOL_PREV, 52, 40, [](lv_event_t *) { console::exec("anim prev"); });
  lv_obj_align(prev, LV_ALIGN_TOP_RIGHT, -116, -4);
  lv_obj_t *next = button(pg, LV_SYMBOL_NEXT, 52, 40, [](lv_event_t *) { console::exec("anim next"); });
  lv_obj_align(next, LV_ALIGN_TOP_RIGHT, -58, -4);
  lv_obj_t *rf = button(
      pg, LV_SYMBOL_REFRESH, 52, 40, [](lv_event_t *) { console::exec("anims"); }, nullptr, C_CYAN);
  lv_obj_align(rf, LV_ALIGN_TOP_RIGHT, 0, -4);

  lv_obj_t *ac = card(pg, 424, 372);
  lv_obj_set_pos(ac, 0, 44);
  animTitle = label(ac, "ANIMATIONS", M8, C_CYAN);
  animGrid = box(ac);
  lv_obj_set_pos(animGrid, 0, 18);
  lv_obj_set_size(animGrid, 402, 332);
  lv_obj_add_flag(animGrid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(animGrid, LV_DIR_VER);
  lv_obj_set_flex_flow(animGrid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(animGrid, 8, 0);

  lv_obj_t *lc = card(pg, 240, 200);
  lv_obj_set_pos(lc, 436, 44);
  label(lc, "LIGHTS", M8, C_CYAN);
  sVisor = sliderRow(lc, "VISOR", 22, &vVisor, C_CYAN);
  sEars = sliderRow(lc, "EARS", 72, &vEars, C_PINK);
  sFan = sliderRow(lc, "FAN", 122, &vFan, C_AMBER);

  lv_obj_t *mc = card(pg, 240, 160);
  lv_obj_set_pos(mc, 436, 256);
  label(mc, "VISOR MODE", M8, C_CYAN);
  static const char *modes[] = {"CUSTOM", "RAINBOW", ""};
  modeBm = lv_buttonmatrix_create(mc);
  lv_buttonmatrix_set_map(modeBm, modes);
  lv_buttonmatrix_set_button_ctrl_all(modeBm, LV_BUTTONMATRIX_CTRL_CHECKABLE);
  lv_buttonmatrix_set_one_checked(modeBm, true);
  lv_obj_set_size(modeBm, 218, 40);
  lv_obj_set_pos(modeBm, 0, 16);
  lv_obj_set_style_pad_all(modeBm, 2, 0);
  lv_obj_set_style_bg_color(modeBm, hex(C_PANEL2), 0);
  lv_obj_set_style_border_width(modeBm, 0, 0);
  lv_obj_set_style_text_font(modeBm, F14, LV_PART_ITEMS);
  lv_obj_add_event_cb(
      modeBm,
      [](lv_event_t *e) {
        uint32_t id = lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
        console::exec(id == 1 ? "visor rainbow" : "visor custom");
      },
      LV_EVENT_VALUE_CHANGED, nullptr);

  struct QA { const char *t; const char *cmd; uint32_t col; };
  static const QA qa[4] = {{LV_SYMBOL_EYE_OPEN " ID", "id", C_CYAN},
                           {LV_SYMBOL_LOOP " NATIVE", "native", C_TEXT},
                           {LV_SYMBOL_POWER " OFF", "off", C_RED},
                           {LV_SYMBOL_SAVE " SAVE", "save", C_GREEN}};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *b = button(
        mc, qa[i].t, 105, 34, [](lv_event_t *e) { console::exec((const char *)lv_event_get_user_data(e)); },
        (void *)qa[i].cmd, qa[i].col);
    lv_obj_set_pos(b, (i % 2) * 113, 66 + (i / 2) * 42);
  }

  ctlNoTarget = box(pg);
  lv_obj_set_size(ctlNoTarget, 704, 444);
  lv_obj_set_pos(ctlNoTarget, -14, -14);
  lv_obj_set_style_bg_color(ctlNoTarget, hex(C_BG), 0);
  lv_obj_set_style_bg_opa(ctlNoTarget, LV_OPA_90, 0);
  lv_obj_add_flag(ctlNoTarget, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *t1 = label(ctlNoTarget, "NO TARGET LOCKED", F24, C_DIM);
  lv_obj_align(t1, LV_ALIGN_CENTER, 0, -30);
  lv_obj_t *go = button(
      ctlNoTarget, LV_SYMBOL_WIFI "  FIND NODES", 200, 44, [](lv_event_t *) { showPage(P_NODES); }, nullptr, C_CYAN);
  lv_obj_align(go, LV_ALIGN_CENTER, 0, 30);
}

void controlRefresh() {
  Peer *p = radio::selectedPeer();
  if (!p) {
    lv_obj_remove_flag(ctlNoTarget, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_add_flag(ctlNoTarget, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text_fmt(ctlSub, "%s  //  %s  //  NOW: %s", p->hello.name, p->hello.kind,
                        p->hasTele ? p->tele.anim : "--");
  static uint32_t lastAnimReq = 0;
  if (p->has(PL_CAP_ANIMS) && !p->animsComplete && millis() - lastAnimReq > 2500 &&
      !radio::pending(radio::selected(), PL_GET)) {
    lastAnimReq = millis();
    radio::getAnims(radio::selected());
  }
  if (animsShownFor != radio::selected() || animsShownCount != p->anims.size()) animsRebuild();
  else if (p->hasTele && !animsShownCurrent.equalsIgnoreCase(p->tele.anim)) animButtonsSync();

  if (p->hasTele) {  // reflect node state unless the user is dragging
    struct { lv_obj_t *s, *v; int val; } rows[3] = {
        {sVisor, vVisor, p->tele.brightVisor}, {sEars, vEars, p->tele.brightEars}, {sFan, vFan, p->tele.fanDuty}};
    for (auto &r : rows) {
      if (lv_obj_has_state(r.s, LV_STATE_PRESSED) || millis() - lastSliderSend < 1500) continue;
      lv_slider_set_value(r.s, r.val, LV_ANIM_OFF);
      lv_label_set_text_fmt(r.v, "%d", r.val);
    }
  }
  bool fan = p->has(PL_CAP_FAN), ears = p->has(PL_CAP_EARS) || p->has(PL_CAP_LEDFX);
  if (fan) lv_obj_remove_state(sFan, LV_STATE_DISABLED); else lv_obj_add_state(sFan, LV_STATE_DISABLED);
  if (ears) lv_obj_remove_state(sEars, LV_STATE_DISABLED); else lv_obj_add_state(sEars, LV_STATE_DISABLED);
}

// ================================================================== POWER
lv_obj_t *pwSub, *tileVal[4], *tileUnit[4], *tileEst[4], *chart, *chartStats, *micBar, *micVal, *pwInfo, *flagBox;
lv_chart_series_t *serI, *serV;
float histI[UI_CHART_POINTS];
int histN = 0, histHead = 0, chartFor = -2;

void chartReset() {
  lv_chart_set_all_value(chart, serI, LV_CHART_POINT_NONE);
  lv_chart_set_all_value(chart, serV, LV_CHART_POINT_NONE);
  histN = histHead = 0;
  chartFor = radio::selected();
}

void buildPower() {
  lv_obj_t *pg = pages[P_POWER] = makePage();
  pageHeader(pg, "POWER", "--", &pwSub);
  static const char *cap[4] = {"BUS VOLTAGE", "CURRENT", "POWER", "CHIP TEMP"};
  static const char *unit[4] = {"V", "A", "W", "C"};
  static const uint32_t col[4] = {C_PINK, C_CYAN, C_TEXT, C_AMBER};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *t = card(pg, 160, 92);
    lv_obj_set_pos(t, i * 172, 44);
    label(t, cap[i], M8, C_DIM);
    tileVal[i] = label(t, "--", MV, col[i]);
    lv_obj_set_pos(tileVal[i], 0, 20);
    tileUnit[i] = label(t, unit[i], F16, C_DIM);
    lv_obj_align_to(tileUnit[i], tileVal[i], LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -4);
    tileEst[i] = chip(t, "EST", C_AMBER);
    lv_obj_align(tileEst[i], LV_ALIGN_TOP_RIGHT, 0, -2);
    lv_obj_add_flag(tileEst[i], LV_OBJ_FLAG_HIDDEN);
    brackets(t, col[i], 8);
  }

  lv_obj_t *cc = card(pg, 676, 194);
  lv_obj_set_pos(cc, 0, 148);
  lv_obj_t *lg1 = label(cc, "\xE2\x97\x8F CURRENT", M8, C_CYAN);  // U+25CF
  lv_obj_set_pos(lg1, 0, 0);
  lv_obj_t *lg2 = label(cc, "\xE2\x97\x8F VOLTAGE", M8, C_PINK);
  lv_obj_set_pos(lg2, 100, 0);
  chartStats = label(cc, "", M8, C_DIM);
  lv_obj_align(chartStats, LV_ALIGN_TOP_RIGHT, 0, 0);

  chart = lv_chart_create(cc);
  lv_obj_set_size(chart, 654, 150);
  lv_obj_set_pos(chart, 0, 18);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart, UI_CHART_POINTS);
  lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_div_line_count(chart, 4, 8);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
  lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 600);
  lv_obj_set_style_bg_color(chart, hex(0x060B10), 0);
  lv_obj_set_style_border_color(chart, hex(C_LINE), 0);
  lv_obj_set_style_line_color(chart, hex(0x13212C), 0);
  lv_obj_set_style_radius(chart, 2, 0);
  lv_obj_set_style_pad_all(chart, 4, 0);
  lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
  lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
  serI = lv_chart_add_series(chart, hex(C_CYAN), LV_CHART_AXIS_PRIMARY_Y);
  serV = lv_chart_add_series(chart, hex(C_PINK), LV_CHART_AXIS_SECONDARY_Y);
  chartReset();

  lv_obj_t *mc = card(pg, 330, 62);
  lv_obj_set_pos(mc, 0, 354);
  label(mc, "VOICE", M8, C_DIM);
  micBar = lv_bar_create(mc);
  lv_obj_set_size(micBar, 250, 12);
  lv_obj_set_pos(micBar, 0, 20);
  lv_bar_set_range(micBar, 0, 100);
  lv_obj_set_style_radius(micBar, 2, 0);
  lv_obj_set_style_radius(micBar, 2, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(micBar, hex(C_PANEL2), 0);
  lv_obj_set_style_bg_color(micBar, hex(C_GREEN), LV_PART_INDICATOR);
  micVal = label(mc, "--", M16, C_GREEN);
  lv_obj_align(micVal, LV_ALIGN_TOP_RIGHT, 0, 16);

  lv_obj_t *ic = card(pg, 334, 62);
  lv_obj_set_pos(ic, 342, 354);
  pwInfo = label(ic, "", M8, C_TEXT);
  lv_obj_set_pos(pwInfo, 0, -3);
  flagBox = box(ic);
  lv_obj_set_size(flagBox, 312, 22);
  lv_obj_set_pos(flagBox, 0, 18);
  lv_obj_set_flex_flow(flagBox, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(flagBox, 6, 0);
}

void fmtNum(char *b, size_t n, float v, int dec) {
  if (isnan(v)) snprintf(b, n, "--");
  else snprintf(b, n, "%.*f", dec, v);
}

void powerOnTelemetry(Peer *p) {
  if (chartFor != radio::selected()) chartReset();
  const PlTelemetry &t = p->tele;
  bool est = t.flags & PL_TF_ESTIMATED;
  char b[16];
  fmtNum(b, sizeof(b), t.busV, 2);
  lv_label_set_text(tileVal[0], b);
  fmtNum(b, sizeof(b), isnan(t.currentmA) ? NAN : t.currentmA / 1000.0f, 2);
  lv_label_set_text(tileVal[1], b);
  fmtNum(b, sizeof(b), isnan(t.powermW) ? NAN : t.powermW / 1000.0f, 1);
  lv_label_set_text(tileVal[2], b);
  fmtNum(b, sizeof(b), t.tempC, 1);
  lv_label_set_text(tileVal[3], b);
  for (int i = 0; i < 4; i++) lv_obj_align_to(tileUnit[i], tileVal[i], LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -4);
  for (int i = 1; i <= 2; i++) {
    if (est) lv_obj_remove_flag(tileEst[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(tileEst[i], LV_OBJ_FLAG_HIDDEN);
  }

  // chart: current (mA, auto-ranged) + voltage (x100, fixed 0..6 V)
  float iA = isnan(t.currentmA) ? 0 : t.currentmA;
  histI[histHead] = iA;
  histHead = (histHead + 1) % UI_CHART_POINTS;
  if (histN < UI_CHART_POINTS) histN++;
  float mx = 0, sum = 0;
  for (int i = 0; i < histN; i++) { mx = fmaxf(mx, histI[i]); sum += histI[i]; }
  int top = (int)(ceilf(fmaxf(mx * 1.25f, 200.0f) / 250.0f) * 250.0f);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, top);
  lv_chart_set_next_value(chart, serI, (int32_t)iA);
  lv_chart_set_next_value(chart, serV, isnan(t.busV) ? LV_CHART_POINT_NONE : (int32_t)(t.busV * 100));
  lv_label_set_text_fmt(chartStats, "PEAK %.2f A   AVG %.2f A   SCALE %.2f A", mx / 1000.0f,
                        histN ? sum / histN / 1000.0f : 0.0f, top / 1000.0f);

  lv_bar_set_value(micBar, t.micLevel, LV_ANIM_ON);
  lv_label_set_text_fmt(micVal, "%3u%%", t.micLevel);
  uint32_t s = t.uptimeMs / 1000;
  lv_label_set_text_fmt(pwInfo, "UP %02lu:%02lu:%02lu HEAP %luK RSSI %d RTT %lu", (unsigned long)(s / 3600),
                        (unsigned long)(s / 60 % 60), (unsigned long)(s % 60), (unsigned long)(t.freeHeap / 1024),
                        p->rssi, (unsigned long)p->rttMs);
  lv_obj_clean(flagBox);
  if (t.flags & PL_TF_TALKING) chip(flagBox, "TALKING", C_GREEN, true);
  if (t.flags & PL_TF_BOOPED) chip(flagBox, "BOOP!", C_PINK, true);
  if (t.flags & PL_TF_TILTED) chip(flagBox, "TILT", C_AMBER, true);
  if (t.flags & PL_TF_SPEECH) chip(flagBox, "MIC ON", C_DIM);
  chip(flagBox, t.anim[0] ? t.anim : "--", C_CYAN);
}

void powerRefresh() {
  Peer *p = radio::selectedPeer();
  if (!p) { lv_label_set_text(pwSub, "NO TARGET"); return; }
  uint32_t age = p->hasTele ? (millis() - p->teleAt) : 0;
  lv_label_set_text_fmt(pwSub, "%s  //  %s  //  %s", p->hello.name,
                        p->hasTele && (p->tele.flags & PL_TF_ESTIMATED) ? "ESTIMATED FROM LED OUTPUT" : "INA219 SENSOR",
                        !p->hasTele ? "WAITING FOR DATA" : age < 2000 ? "LIVE" : "STALE");
}

// ================================================================== FX
Preset edit;
int editSlot = 0, colorTarget = 0;
bool fxLive = false, fxLoading = false;
uint32_t lastLiveSend = 0;
lv_obj_t *slotBtns[PRESET_SLOTS], *slotSw[PRESET_SLOTS], *fxName, *fxSlotLbl, *zoneBm, *fxDd, *swA, *swB, *hexLbl,
    *sHue, *sSat, *sVal, *sSpeed, *sBri, *vSpeed, *vBri, *leds[24], *liveSw;

void rgb2hsv(uint8_t r, uint8_t g, uint8_t b, uint8_t *o) {
  uint8_t mx = max(r, max(g, b)), mn = min(r, min(g, b));
  o[2] = mx;
  if (!mx) { o[0] = o[1] = 0; return; }
  o[1] = (uint8_t)(255 * (mx - mn) / mx);
  if (mx == mn) { o[0] = 0; return; }
  int h;
  if (mx == r) h = 0 + 43 * (g - b) / (mx - mn);
  else if (mx == g) h = 85 + 43 * (b - r) / (mx - mn);
  else h = 171 + 43 * (r - g) / (mx - mn);
  o[0] = (uint8_t)(h < 0 ? h + 256 : h);
}

void slotSwatch(lv_obj_t *sw, const Preset &p) {
  static lv_grad_dsc_t g[PRESET_SLOTS];
  int idx = 0;
  for (int i = 0; i < PRESET_SLOTS; i++)
    if (slotSw[i] == sw) idx = i;
  lv_grad_dsc_t &gd = g[idx];
  memset(&gd, 0, sizeof(gd));
  gd.dir = LV_GRAD_DIR_HOR;
  gd.stops_count = 2;
  gd.stops[0].color = lv_color_make(p.fx.r1, p.fx.g1, p.fx.b1);
  gd.stops[1].color = lv_color_make(p.fx.r2, p.fx.g2, p.fx.b2);
  gd.stops[0].opa = gd.stops[1].opa = LV_OPA_COVER;
  gd.stops[0].frac = 0;
  gd.stops[1].frac = 255;
  if (p.fx.fx == PL_FX_RAINBOW) {
    static const uint32_t rb[6] = {0xFF0000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x0000FF, 0xFF00FF};
    gd.stops_count = 6;
    for (int i = 0; i < 6; i++) {
      gd.stops[i].color = hex(rb[i]);
      gd.stops[i].opa = LV_OPA_COVER;
      gd.stops[i].frac = i * 51;
    }
  }
  lv_obj_set_style_bg_grad(sw, &gd, 0);
  lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
}

void fxSyncSwatches() {
  auto lum = [](uint8_t r, uint8_t g, uint8_t b) { return (r * 3 + g * 6 + b) / 10; };
  lv_obj_set_style_bg_color(swA, lv_color_make(edit.fx.r1, edit.fx.g1, edit.fx.b1), 0);
  lv_obj_set_style_bg_color(swB, lv_color_make(edit.fx.r2, edit.fx.g2, edit.fx.b2), 0);
  lv_obj_set_style_text_color(swA, hex(lum(edit.fx.r1, edit.fx.g1, edit.fx.b1) > 110 ? C_BG : C_TEXT), 0);
  lv_obj_set_style_text_color(swB, hex(lum(edit.fx.r2, edit.fx.g2, edit.fx.b2) > 110 ? C_BG : C_TEXT), 0);
  lv_obj_set_style_border_color(swA, hex(colorTarget == 0 ? C_TEXT : C_LINE), 0);
  lv_obj_set_style_border_color(swB, hex(colorTarget == 1 ? C_TEXT : C_LINE), 0);
  lv_obj_set_style_border_width(swA, colorTarget == 0 ? 3 : 1, 0);
  lv_obj_set_style_border_width(swB, colorTarget == 1 ? 3 : 1, 0);
  uint8_t *c = colorTarget ? &edit.fx.r2 : &edit.fx.r1;
  lv_label_set_text_fmt(hexLbl, "%s  #%02X%02X%02X", colorTarget ? "B" : "A", c[0], c[1], c[2]);
}

void fxLoadHsvSliders() {
  uint8_t *c = colorTarget ? &edit.fx.r2 : &edit.fx.r1;
  uint8_t h[3];
  rgb2hsv(c[0], c[1], c[2], h);
  lv_slider_set_value(sHue, h[0], LV_ANIM_OFF);
  lv_slider_set_value(sSat, h[1], LV_ANIM_OFF);
  lv_slider_set_value(sVal, h[2], LV_ANIM_OFF);
}

void fxLoadEditor() {
  fxLoading = true;
  char nameCopy[sizeof(edit.name)];  // set_text fires VALUE_CHANGED per char; never alias edit.name
  memcpy(nameCopy, edit.name, sizeof(nameCopy));
  nameCopy[sizeof(nameCopy) - 1] = 0;
  lv_textarea_set_text(fxName, nameCopy);
  lv_label_set_text_fmt(fxSlotLbl, "SLOT %02d", editSlot);
  lv_buttonmatrix_set_button_ctrl_all(zoneBm, LV_BUTTONMATRIX_CTRL_CHECKABLE);
  for (int i = 0; i < 4; i++) lv_buttonmatrix_clear_button_ctrl(zoneBm, i, LV_BUTTONMATRIX_CTRL_CHECKED);
  lv_buttonmatrix_set_button_ctrl(zoneBm, edit.fx.zone < 4 ? edit.fx.zone : 0, LV_BUTTONMATRIX_CTRL_CHECKED);
  lv_dropdown_set_selected(fxDd, edit.fx.fx);
  lv_slider_set_value(sSpeed, edit.fx.speed, LV_ANIM_OFF);
  lv_slider_set_value(sBri, edit.fx.brightness, LV_ANIM_OFF);
  lv_label_set_text_fmt(vSpeed, "%u", edit.fx.speed);
  if (edit.fx.brightness) lv_label_set_text_fmt(vBri, "%u", edit.fx.brightness);
  else lv_label_set_text(vBri, "KEEP");
  for (int i = 0; i < PRESET_SLOTS; i++) {
    if (i == editSlot) lv_obj_add_state(slotBtns[i], LV_STATE_CHECKED);
    else lv_obj_remove_state(slotBtns[i], LV_STATE_CHECKED);
  }
  fxSyncSwatches();
  fxLoadHsvSliders();
  fxLoading = false;
}

void fxSend(bool echo) {
  int p = radio::selected();
  if (p < 0) { toast("NO TARGET LOCKED", C_AMBER); return; }
  radio::ledFx(p, edit.fx);
  if (echo)
    console::print(CON_DIM, "-> fx %s %s #%02X%02X%02X #%02X%02X%02X spd %u", plZoneName(edit.fx.zone),
                   plFxName(edit.fx.fx), edit.fx.r1, edit.fx.g1, edit.fx.b1, edit.fx.r2, edit.fx.g2, edit.fx.b2,
                   edit.fx.speed);
}

void fxChanged() {
  if (fxLive && millis() - lastLiveSend > 150) {
    lastLiveSend = millis();
    fxSend(false);
  }
}

void hsvEvent(lv_event_t *) {
  uint8_t rgb[3];
  plfx::hsv(lv_slider_get_value(sHue), lv_slider_get_value(sSat), lv_slider_get_value(sVal), rgb);
  uint8_t *c = colorTarget ? &edit.fx.r2 : &edit.fx.r1;
  memcpy(c, rgb, 3);
  fxSyncSwatches();
  fxChanged();
}

lv_obj_t *fxSlider(lv_obj_t *parent, const char *name, uint32_t color, lv_obj_t **valOut) {
  lv_obj_t *row = box(parent);
  lv_obj_set_size(row, 336, 24);
  lv_obj_t *n = label(row, name, M8, C_DIM);
  lv_obj_align(n, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t *s = slider(row, 230, 0, 255, color);
  lv_obj_align(s, LV_ALIGN_LEFT_MID, 52, 0);
  if (valOut) {
    *valOut = label(row, "", M8, color);
    lv_obj_align(*valOut, LV_ALIGN_RIGHT_MID, 0, 0);
  }
  return s;
}

void buildFx() {
  lv_obj_t *pg = pages[P_FX] = makePage();

  // ---- left: preset slots
  lv_obj_t *lc = card(pg, 300, 416);
  lv_obj_set_pos(lc, 0, 0);
  label(lc, "PRESETS", F20, C_TEXT);
  lv_obj_t *hint = label(lc, "tap to edit", M8, C_DIM);
  lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, 0, 6);
  for (int i = 0; i < PRESET_SLOTS; i++) {
    lv_obj_t *b = button(
        lc, presets::get(i).name, 134, 50,
        [](lv_event_t *e) {
          editSlot = (int)(intptr_t)lv_event_get_user_data(e);
          edit = presets::get(editSlot);
          fxLoadEditor();
        },
        (void *)(intptr_t)i);
    lv_obj_set_pos(b, (i % 2) * 144, 34 + (i / 2) * 60);
    lv_obj_t *l = lv_obj_get_child(b, 0);
    lv_obj_set_style_text_font(l, F12, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 6);
    slotSw[i] = box(b);
    lv_obj_set_size(slotSw[i], 112, 6);
    lv_obj_align(slotSw[i], LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_radius(slotSw[i], 3, 0);
    slotSwatch(slotSw[i], presets::get(i));
    slotBtns[i] = b;
  }

  // ---- right: editor (scrolls if the keyboard is open)
  lv_obj_t *ed = card(pg, 364, 416);
  lv_obj_set_pos(ed, 312, 0);
  lv_obj_add_flag(ed, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(ed, LV_DIR_VER);
  lv_obj_set_flex_flow(ed, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(ed, 6, 0);

  lv_obj_t *r1 = box(ed);
  lv_obj_set_size(r1, 336, 36);
  fxName = lv_textarea_create(r1);
  lv_textarea_set_one_line(fxName, true);
  lv_textarea_set_max_length(fxName, 15);
  lv_obj_set_size(fxName, 230, 36);
  lv_obj_set_style_text_font(fxName, F14, 0);
  lv_obj_set_style_pad_ver(fxName, 8, 0);
  kbAttach(fxName);
  lv_obj_add_event_cb(
      fxName,
      [](lv_event_t *) {
        if (fxLoading) return;
        String n = lv_textarea_get_text(fxName);
        n.toUpperCase();
        strncpy(edit.name, n.c_str(), sizeof(edit.name) - 1);
        edit.name[sizeof(edit.name) - 1] = 0;
      },
      LV_EVENT_VALUE_CHANGED, nullptr);
  fxSlotLbl = label(r1, "SLOT 00", M8, C_PINK);
  lv_obj_align(fxSlotLbl, LV_ALIGN_RIGHT_MID, 0, 0);

  static const char *zones[] = {"ALL", "VISOR", "EARS", "AUX", ""};
  zoneBm = lv_buttonmatrix_create(ed);
  lv_buttonmatrix_set_map(zoneBm, zones);
  lv_buttonmatrix_set_button_ctrl_all(zoneBm, LV_BUTTONMATRIX_CTRL_CHECKABLE);
  lv_buttonmatrix_set_one_checked(zoneBm, true);
  lv_obj_set_size(zoneBm, 336, 34);
  lv_obj_set_style_pad_all(zoneBm, 2, 0);
  lv_obj_set_style_border_width(zoneBm, 0, 0);
  lv_obj_set_style_bg_color(zoneBm, hex(C_PANEL2), 0);
  lv_obj_set_style_text_font(zoneBm, F12, LV_PART_ITEMS);
  lv_obj_add_event_cb(
      zoneBm,
      [](lv_event_t *e) {
        edit.fx.zone = (uint8_t)lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
        fxChanged();
      },
      LV_EVENT_VALUE_CHANGED, nullptr);

  fxDd = lv_dropdown_create(ed);
  String opts;
  for (int i = 0; i < PL_FX_COUNT; i++) {
    opts += plFxName(i);
    if (i < PL_FX_COUNT - 1) opts += "\n";
  }
  lv_dropdown_set_options(fxDd, opts.c_str());
  lv_obj_set_size(fxDd, 336, 38);
  lv_obj_set_style_pad_ver(fxDd, 9, 0);
  lv_obj_set_style_text_font(fxDd, F14, 0);
  lv_obj_add_event_cb(
      fxDd,
      [](lv_event_t *e) {
        edit.fx.fx = (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e));
        fxChanged();
      },
      LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *cr = box(ed);
  lv_obj_set_size(cr, 336, 36);
  swA = button(
      cr, "A", 70, 36, [](lv_event_t *) { colorTarget = 0; fxSyncSwatches(); fxLoadHsvSliders(); });
  swB = button(
      cr, "B", 70, 36, [](lv_event_t *) { colorTarget = 1; fxSyncSwatches(); fxLoadHsvSliders(); });
  lv_obj_set_pos(swA, 0, 0);
  lv_obj_set_pos(swB, 80, 0);
  for (lv_obj_t *s : {swA, swB}) {
    lv_obj_remove_style(s, &stBtnPr, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(s, hex(C_BG), 0);
  }
  hexLbl = label(cr, "", M16, C_TEXT);
  lv_obj_align(hexLbl, LV_ALIGN_RIGHT_MID, 0, 0);

  sHue = fxSlider(ed, "HUE", C_TEXT, nullptr);
  static lv_grad_dsc_t rainbow;
  memset(&rainbow, 0, sizeof(rainbow));
  rainbow.dir = LV_GRAD_DIR_HOR;
  rainbow.stops_count = 7;
  static const uint32_t rc[7] = {0xFF0000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x0000FF, 0xFF00FF, 0xFF0000};
  for (int i = 0; i < 7; i++) {
    rainbow.stops[i].color = hex(rc[i]);
    rainbow.stops[i].opa = LV_OPA_COVER;
    rainbow.stops[i].frac = i * 42;
  }
  lv_obj_set_style_bg_grad(sHue, &rainbow, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sHue, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sHue, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sHue, hex(C_TEXT), LV_PART_KNOB);
  sSat = fxSlider(ed, "SAT", C_TEXT, nullptr);
  sVal = fxSlider(ed, "VAL", C_TEXT, nullptr);
  for (lv_obj_t *s : {sHue, sSat, sVal}) lv_obj_add_event_cb(s, hsvEvent, LV_EVENT_VALUE_CHANGED, nullptr);

  sSpeed = fxSlider(ed, "SPEED", C_CYAN, &vSpeed);
  lv_obj_add_event_cb(
      sSpeed,
      [](lv_event_t *e) {
        edit.fx.speed = lv_slider_get_value(lv_event_get_target_obj(e));
        lv_label_set_text_fmt(vSpeed, "%u", edit.fx.speed);
        fxChanged();
      },
      LV_EVENT_VALUE_CHANGED, nullptr);
  sBri = fxSlider(ed, "BRIGHT", C_PINK, &vBri);
  lv_obj_add_event_cb(
      sBri,
      [](lv_event_t *e) {
        edit.fx.brightness = lv_slider_get_value(lv_event_get_target_obj(e));
        if (edit.fx.brightness) lv_label_set_text_fmt(vBri, "%u", edit.fx.brightness);
        else lv_label_set_text(vBri, "KEEP");
        fxChanged();
      },
      LV_EVENT_VALUE_CHANGED, nullptr);

  // live preview strip
  lv_obj_t *pv = box(ed);
  lv_obj_set_size(pv, 336, 22);
  lv_obj_set_style_bg_color(pv, hex(0x020406), 0);
  lv_obj_set_style_bg_opa(pv, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(pv, 12, 0);
  for (int i = 0; i < 24; i++) {
    leds[i] = box(pv);
    lv_obj_set_size(leds[i], 10, 10);
    lv_obj_set_style_radius(leds[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(leds[i], LV_OPA_COVER, 0);
    lv_obj_set_pos(leds[i], 8 + i * 13, 6);
  }

  lv_obj_t *br = box(ed);
  lv_obj_set_size(br, 336, 40);
  lv_obj_t *apply = button(
      br, LV_SYMBOL_UPLOAD " APPLY", 120, 40, [](lv_event_t *) { fxSend(true); }, nullptr, C_CYAN);
  lv_obj_set_pos(apply, 0, 0);
  lv_obj_t *save = button(
      br, LV_SYMBOL_SAVE " SAVE", 100, 40,
      [](lv_event_t *) {
        presets::put(editSlot, edit);
        lv_label_set_text(lv_obj_get_child(slotBtns[editSlot], 0), edit.name);
        slotSwatch(slotSw[editSlot], edit);
        char m[40];
        snprintf(m, sizeof(m), "SAVED TO SLOT %d", editSlot);
        toast(m, C_GREEN);
      },
      nullptr, C_PINK);
  lv_obj_set_pos(save, 128, 0);
  liveSw = lv_switch_create(br);
  lv_obj_set_size(liveSw, 50, 26);
  lv_obj_align(liveSw, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(
      liveSw, [](lv_event_t *e) { fxLive = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED); },
      LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_t *ll = label(br, "LIVE", M8, C_DIM);
  lv_obj_align(ll, LV_ALIGN_RIGHT_MID, -58, 0);

  edit = presets::get(0);
  fxLoadEditor();
}

void fxPreviewTick() {
  static uint8_t buf[24 * 3];
  uint32_t now = millis();
  if (edit.fx.fx == PL_FX_ANIM) {  // "native": show a calm dim pattern
    for (int i = 0; i < 24; i++) lv_obj_set_style_bg_color(leds[i], hex(i % 3 ? 0x101820 : 0x1B2B38), 0);
    return;
  }
  plfx::render(edit.fx, buf, 24, now);
  uint8_t bri = edit.fx.brightness ? edit.fx.brightness : 255;
  for (int i = 0; i < 24; i++)
    lv_obj_set_style_bg_color(
        leds[i],
        lv_color_make(plfx::scale8(buf[i * 3], bri), plfx::scale8(buf[i * 3 + 1], bri), plfx::scale8(buf[i * 3 + 2], bri)),
        0);
}

// ================================================================== TERM
lv_obj_t *termOut, *termIn = nullptr;
bool termDirty = false;
int histPos = -1;
struct PendingLine { String text; uint32_t color; };
std::vector<PendingLine> earlyLines;  // printed before the UI existed (boot)

void termAdd(const char *line, uint32_t color) {
  if (lv_obj_get_child_count(termOut) >= 140) lv_obj_delete(lv_obj_get_child(termOut, 0));
  lv_obj_t *l = lv_label_create(termOut);
  lv_label_set_text(l, line[0] ? line : " ");
  lv_obj_set_width(l, 652);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(l, M16, 0);
  lv_obj_set_style_text_color(l, hex(color), 0);
  termDirty = true;
}

void termRun() {
  String cmd = lv_textarea_get_text(termIn);
  lv_textarea_set_text(termIn, "");
  histPos = -1;
  if (cmd.length()) console::exec(cmd);
}

void buildTerm() {
  lv_obj_t *pg = pages[P_TERM] = makePage();
  lv_obj_set_flex_flow(pg, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(pg, 8, 0);

  termOut = lv_obj_create(pg);
  lv_obj_remove_style_all(termOut);
  lv_obj_set_width(termOut, 676);
  lv_obj_set_flex_grow(termOut, 1);
  lv_obj_set_style_bg_color(termOut, hex(0x020407), 0);
  lv_obj_set_style_bg_opa(termOut, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(termOut, 1, 0);
  lv_obj_set_style_border_color(termOut, hex(C_LINE), 0);
  lv_obj_set_style_radius(termOut, 4, 0);
  lv_obj_set_style_pad_all(termOut, 8, 0);
  lv_obj_set_flex_flow(termOut, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(termOut, 1, 0);
  lv_obj_add_flag(termOut, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(termOut, LV_DIR_VER);

  lv_obj_t *chips = box(pg);
  lv_obj_set_size(chips, 676, 32);
  lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(chips, 6, 0);
  static const char *quick[] = {"help", "scan", "ls", "tele", "anims", "preset ls", "say status", "neofetch", "clear"};
  for (auto q : quick) {
    lv_obj_t *b = button(
        chips, q, LV_SIZE_CONTENT, 30, [](lv_event_t *e) { console::exec((const char *)lv_event_get_user_data(e)); },
        (void *)q, C_CYAN);
    lv_obj_set_style_pad_hor(b, 9, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), M8, 0);
  }

  lv_obj_t *ir = box(pg);
  lv_obj_set_size(ir, 676, 42);
  lv_obj_t *pr = label(ir, "deck>", M16, C_PINK);
  lv_obj_align(pr, LV_ALIGN_LEFT_MID, 0, 0);
  termIn = lv_textarea_create(ir);
  lv_textarea_set_one_line(termIn, true);
  lv_textarea_set_placeholder_text(termIn, "type a command (help)");
  lv_obj_set_size(termIn, 440, 42);
  lv_obj_set_pos(termIn, 52, 0);
  lv_obj_set_style_text_font(termIn, M16, 0);
  lv_obj_set_style_bg_color(termIn, hex(0x020407), 0);
  lv_obj_set_style_pad_ver(termIn, 12, 0);
  kbAttach(termIn);
  lv_obj_add_event_cb(termIn, [](lv_event_t *) { termRun(); }, LV_EVENT_READY, nullptr);

  auto histNav = [](lv_event_t *e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    auto &h = console::history();
    if (h.empty()) return;
    if (histPos < 0) histPos = h.size();
    histPos = constrain(histPos + dir, 0, (int)h.size());
    lv_textarea_set_text(termIn, histPos < (int)h.size() ? h[histPos].c_str() : "");
  };
  lv_obj_t *up = button(ir, LV_SYMBOL_UP, 44, 42, histNav, (void *)(intptr_t)-1);
  lv_obj_set_pos(up, 500, 0);
  lv_obj_t *dn = button(ir, LV_SYMBOL_DOWN, 44, 42, histNav, (void *)(intptr_t)1);
  lv_obj_set_pos(dn, 550, 0);
  lv_obj_t *run = button(
      ir, "RUN", 72, 42, [](lv_event_t *) { termRun(); }, nullptr, C_CYAN);
  lv_obj_set_pos(run, 604, 0);

  for (auto &l : earlyLines) termAdd(l.text.c_str(), l.color);
  earlyLines.clear();
}

// ================================================================== SYS
lv_obj_t *sysInfo, *radioInfo;

lv_obj_t *switchRow(lv_obj_t *parent, const char *name, int y, bool on, lv_event_cb_t cb) {
  lv_obj_t *l = label(parent, name, F14, C_TEXT);
  lv_obj_set_pos(l, 0, y + 4);
  lv_obj_t *s = lv_switch_create(parent);
  lv_obj_set_size(s, 50, 26);
  lv_obj_align(s, LV_ALIGN_TOP_RIGHT, 0, y);
  if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
  lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, nullptr);
  return s;
}

void buildSys() {
  lv_obj_t *pg = pages[P_SYS] = makePage();
  settings::Settings &st = settings::get();

  lv_obj_t *dc = card(pg, 330, 228);
  lv_obj_set_pos(dc, 0, 0);
  label(dc, "DISPLAY", M8, C_CYAN);
  lv_obj_t *bl = label(dc, "BACKLIGHT", F14, C_TEXT);
  lv_obj_set_pos(bl, 0, 20);
  lv_obj_t *bs = slider(dc, 300, 5, 100, C_CYAN);
  lv_obj_set_pos(bs, 2, 46);
  lv_slider_set_value(bs, st.backlight, LV_ANIM_OFF);
  lv_obj_add_event_cb(
      bs,
      [](lv_event_t *e) {
        uint8_t v = lv_slider_get_value(lv_event_get_target_obj(e));
        board::setBacklight(v);
        settings::get().backlight = v;
        if (lv_event_get_code(e) == LV_EVENT_RELEASED) settings::save();
      },
      LV_EVENT_ALL, nullptr);

  lv_obj_t *sl = label(dc, "SLEEP AFTER", F14, C_TEXT);
  lv_obj_set_pos(sl, 0, 72);
  lv_obj_t *sd = lv_dropdown_create(dc);
  lv_dropdown_set_options(sd, "never\n30 s\n1 min\n2 min\n5 min\n10 min");
  lv_obj_set_size(sd, 120, 36);
  lv_obj_align(sd, LV_ALIGN_TOP_RIGHT, 0, 64);
  static const uint16_t sleepOpts[6] = {0, 30, 60, 120, 300, 600};
  for (int i = 0; i < 6; i++)
    if (sleepOpts[i] == st.sleepSec) lv_dropdown_set_selected(sd, i);
  lv_obj_add_event_cb(
      sd,
      [](lv_event_t *e) {
        settings::get().sleepSec = sleepOpts[lv_dropdown_get_selected(lv_event_get_target_obj(e))];
        settings::save();
      },
      LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *cs = switchRow(dc, board::revision() >= 13 ? "TOUCH CLICKS" : "TOUCH CLICKS (V1.3+)", 112, st.clicks,
                           [](lv_event_t *e) {
                             bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
                             board::setClicks(on);
                             settings::get().clicks = on;
                             settings::save();
                           });
  if (board::revision() < 13) lv_obj_add_state(cs, LV_STATE_DISABLED);
  switchRow(dc, "SKIP BOOT SEQUENCE", 150, st.skipBoot, [](lv_event_t *e) {
    settings::get().skipBoot = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings::save();
  });
  switchRow(dc, "SCAN ON BOOT", 186, st.scanOnBoot, [](lv_event_t *e) {
    settings::get().scanOnBoot = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings::save();
  });

  lv_obj_t *rc = card(pg, 334, 228);
  lv_obj_set_pos(rc, 342, 0);
  label(rc, "RADIO", M8, C_CYAN);
  lv_obj_t *hl = label(rc, "HOME CHANNEL", F14, C_TEXT);
  lv_obj_set_pos(hl, 0, 24);
  lv_obj_t *hd = lv_dropdown_create(rc);
  lv_dropdown_set_options(hd, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13");
  lv_obj_set_size(hd, 90, 36);
  lv_obj_align(hd, LV_ALIGN_TOP_RIGHT, 0, 16);
  lv_dropdown_set_selected(hd, st.homeChannel - 1);
  lv_obj_add_event_cb(
      hd,
      [](lv_event_t *e) {
        uint8_t ch = lv_dropdown_get_selected(lv_event_get_target_obj(e)) + 1;
        settings::get().homeChannel = ch;
        settings::save();
        if (!radio::selectedPeer()) radio::setChannel(ch);
      },
      LV_EVENT_VALUE_CHANGED, nullptr);
  radioInfo = label(rc, "", M8, C_DIM);
  lv_obj_set_pos(radioInfo, 0, 66);
  lv_obj_t *sb = button(
      rc, LV_SYMBOL_REFRESH " FULL SWEEP", 150, 40, [](lv_event_t *) { console::exec("scan"); }, nullptr, C_CYAN);
  lv_obj_align(sb, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t *pb = button(
      rc, LV_SYMBOL_BELL " PING", 110, 40, [](lv_event_t *) { console::exec("ping"); }, nullptr, C_TEXT);
  lv_obj_align(pb, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  lv_obj_t *yc = card(pg, 676, 176);
  lv_obj_set_pos(yc, 0, 240);
  label(yc, "SYSTEM", M8, C_CYAN);
  sysInfo = label(yc, "", M8, C_TEXT);
  lv_obj_set_pos(sysInfo, 0, 20);
  lv_obj_set_style_text_line_space(sysInfo, 6, 0);
  lv_obj_t *rb = button(
      yc, LV_SYMBOL_PLAY " REPLAY BOOT", 170, 40, [](lv_event_t *) { bootseq::start([]() { show(true); }); },
      nullptr, C_PINK);
  lv_obj_align(rb, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t *rp = button(
      yc, LV_SYMBOL_TRASH " RESET PRESETS", 190, 40,
      [](lv_event_t *) {
        presets::resetDefaults();
        for (int i = 0; i < PRESET_SLOTS; i++) {
          lv_label_set_text(lv_obj_get_child(slotBtns[i], 0), presets::get(i).name);
          slotSwatch(slotSw[i], presets::get(i));
        }
        edit = presets::get(editSlot);
        fxLoadEditor();
        toast("PRESETS RESET", C_AMBER);
      },
      nullptr, C_AMBER);
  lv_obj_align(rp, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_t *rr = button(
      yc, LV_SYMBOL_POWER " REBOOT", 140, 40, [](lv_event_t *) { console::exec("reboot deck"); }, nullptr, C_RED);
  lv_obj_align(rr, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

void sysRefresh() {
  const board::ProbeReport &pr = board::probe();
  lv_label_set_text_fmt(sysInfo,
                        "%s v%s   CrowPanel Advance 7.0 rev %u.%u   ESP32-S3 @ %lu MHz\n"
                        "SRAM free %lu KB   PSRAM free %lu / %lu KB   flash %lu MB\n"
                        "touch %s   helper MCU %s   RTC %s   uptime %lus",
                        DECK_NAME, DECK_VERSION, board::revision() / 10, board::revision() % 10,
                        (unsigned long)ESP.getCpuFreqMHz(),
                        (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)(ESP.getFreePsram() / 1024),
                        (unsigned long)(ESP.getPsramSize() / 1024), (unsigned long)(ESP.getFlashChipSize() >> 20),
                        pr.touch ? "GT911 OK" : "MISSING", pr.helperMcu ? "STC8 OK" : pr.ioExpander ? "IOEX" : "none",
                        pr.rtc ? "PCF8563" : "none", (unsigned long)(millis() / 1000));
  lv_label_set_text_fmt(radioInfo, "MAC   %s\nNETID 0x%04X   CH %u\nTX %lu  RX %lu  LOST %lu",
                        radio::myMac().c_str(), PL_NET_ID, radio::channel(), (unsigned long)radio::txCount(),
                        (unsigned long)radio::rxCount(), (unsigned long)radio::lostCount());
}

// ================================================================== misc
void showPage(int i) {
  if (i < 0 || i >= P_COUNT) return;
  kbHide();
  for (int k = 0; k < P_COUNT; k++) {
    if (k == i) {
      lv_obj_remove_flag(pages[k], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_state(navBtns[k], LV_STATE_CHECKED);
    } else {
      lv_obj_add_flag(pages[k], LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_state(navBtns[k], LV_STATE_CHECKED);
    }
  }
  curPage = i;
  if (i == P_CTRL) {
    Peer *p = radio::selectedPeer();
    if (p && p->has(PL_CAP_ANIMS) && !p->animsComplete && !radio::pending(radio::selected(), PL_GET))
      radio::getAnims(radio::selected());
    controlRefresh();
  }
  if (i == P_POWER) powerRefresh();
  if (i == P_SYS) sysRefresh();
  if (i == P_TERM) termDirty = true;
}

void tick(lv_timer_t *) {
  static uint32_t lastPoll = 0, lastSlow = 0, lastBgPing = 0;
  static int bgIdx = 0;
  uint32_t now = millis();
  refreshTopBar();

  // scan strip
  if (radio::scanning()) {
    lv_bar_set_value(scanBar, radio::scanProgress(), LV_ANIM_ON);
    lv_label_set_text_fmt(scanLbl, "SWEEPING  //  CH %02u  //  LISTENING...", radio::channel());
    lv_obj_add_state(scanBtn, LV_STATE_DISABLED);
  } else {
    lv_obj_remove_state(scanBtn, LV_STATE_DISABLED);
    if (lastScanEnd)
      lv_label_set_text_fmt(scanLbl, "IDLE  //  LAST SWEEP %lus AGO  //  %d NODE%s KNOWN",
                            (unsigned long)((now - lastScanEnd) / 1000), radio::count(), radio::count() == 1 ? "" : "S");
  }

  // telemetry polling for the target node
  int sel = radio::selected();
  Peer *p = radio::selectedPeer();
  uint32_t rate = (curPage == P_POWER || curPage == P_CTRL) ? UI_TELEMETRY_FAST_MS : UI_TELEMETRY_SLOW_MS;
  if (p && !radio::scanning() && now - lastPoll >= rate && !radio::pending(sel, PL_GET)) {
    lastPoll = now;
    radio::getTelemetry(sel);
  }
  // keep other nodes' "online" status fresh: ping one every 3 s (same channel only)
  if (!radio::scanning() && radio::count() > 1 && now - lastBgPing > 3000) {
    lastBgPing = now;
    for (int k = 0; k < radio::count(); k++) {
      bgIdx = (bgIdx + 1) % radio::count();
      Peer *q = radio::peer(bgIdx);
      if (bgIdx != sel && q->hello.channel == radio::channel() && !radio::pending(bgIdx, PL_PING)) {
        radio::ping(bgIdx);
        break;
      }
    }
  }

  if (now - lastSlow > 1000) {
    lastSlow = now;
    if (curPage == P_NODES) nodesRefresh();
    if (curPage == P_SYS) sysRefresh();
    if (curPage == P_POWER) powerRefresh();
  }
  if (curPage == P_CTRL) controlRefresh();

  if (termDirty && curPage == P_TERM) {
    termDirty = false;
    lv_obj_update_layout(termOut);
    lv_obj_scroll_to_y(termOut, LV_COORD_MAX, LV_ANIM_OFF);
  }

  // screen sleep
  uint16_t sl = settings::get().sleepSec;
  if (sl && !board::asleep() && lv_display_get_inactive_time(nullptr) > (uint32_t)sl * 1000) board::sleep();
}

void fastTick(lv_timer_t *) {
  if (curPage == P_FX) fxPreviewTick();
}

}  // namespace

// ================================================================== public
void toast(const char *text, uint32_t color) {
  if (!toastBox || bootseq::running()) return;
  lv_label_set_text(toastLbl, text);
  lv_obj_set_style_text_color(toastLbl, hex(color), 0);
  lv_obj_set_style_border_color(toastBox, hex(color), 0);
  lv_obj_remove_flag(toastBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(toastBox);
  lv_anim_delete(toastBox, nullptr);
  lv_obj_set_style_opa(toastBox, LV_OPA_COVER, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, toastBox);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_duration(&a, 400);
  lv_anim_set_delay(&a, 1600);
  lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, v, 0); });
  lv_anim_set_completed_cb(&a, [](lv_anim_t *an) { lv_obj_add_flag((lv_obj_t *)an->var, LV_OBJ_FLAG_HIDDEN); });
  lv_anim_start(&a);
}

void build() {
  initStyles();
  lv_display_t *d = lv_display_get_default();
  lv_theme_t *th = lv_theme_default_init(d, hex(C_CYAN), hex(C_PINK), true, F16);
  lv_display_set_theme(d, th);

  scr = lv_obj_create(nullptr);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(scr, hex(C_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  buildTopBar();
  buildNav();
  content = box(scr);
  lv_obj_set_pos(content, 96, 36);
  lv_obj_set_size(content, 704, 444);
  buildNodes();
  buildControl();
  buildPower();
  buildFx();
  buildTerm();
  buildSys();

  kb = lv_keyboard_create(scr);
  lv_obj_set_size(kb, 704, 220);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_text_font(kb, F16, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, hex(C_PANEL), 0);
  lv_obj_set_style_border_side(kb, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(kb, hex(C_LINE), 0);
  lv_obj_set_style_border_width(kb, 1, 0);
  lv_obj_set_style_bg_color(kb, hex(C_PANEL2), LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb, hex(C_TEXT), LV_PART_ITEMS);
  lv_obj_set_style_border_color(kb, hex(C_LINE), LV_PART_ITEMS);
  lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, hex(0x16283A), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(kb, hex(C_CYAN), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(kb, hex(C_CYAN), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(kb, hex(C_BG), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(
      kb,
      [](lv_event_t *e) {
        lv_event_code_t c = lv_event_get_code(e);
        // READY reaches the keyboard before the textarea: only hide the widget here and
        // keep the textarea attached so it still receives READY (the terminal keeps typing)
        if (c == LV_EVENT_CANCEL) kbHide();
        else if (c == LV_EVENT_READY && lv_keyboard_get_textarea(kb) != termIn) {
          lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
      },
      LV_EVENT_ALL, nullptr);

  toastBox = box(lv_layer_top());
  lv_obj_set_size(toastBox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(toastBox, hex(C_PANEL2), 0);
  lv_obj_set_style_bg_opa(toastBox, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(toastBox, 1, 0);
  lv_obj_set_style_radius(toastBox, 4, 0);
  lv_obj_set_style_pad_hor(toastBox, 14, 0);
  lv_obj_set_style_pad_ver(toastBox, 10, 0);
  lv_obj_align(toastBox, LV_ALIGN_TOP_RIGHT, -12, 46);
  toastLbl = label(toastBox, "", M16, C_CYAN);
  lv_obj_add_flag(toastBox, LV_OBJ_FLAG_HIDDEN);

  showPage(P_NODES);
  lv_timer_create(tick, 250, nullptr);
  lv_timer_create(fastTick, 40, nullptr);
  built = true;
}

void show(bool animated) {
  if (!scr) return;
  if (animated) lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 450, 0, false);
  else lv_screen_load(scr);
}

void onRadio(const RadioEvent &e) {
  if (!built) return;
  switch (e.type) {
    case RadioEvt::PeerFound:
      nodesRebuild();
      if (radio::count() == 1) {
        char m[48];
        snprintf(m, sizeof(m), "NODE FOUND: %s", radio::peer(e.peer)->hello.name);
        toast(m, C_GREEN);
      }
      break;
    case RadioEvt::PeerUpdated:
      if (e.peer < nCards) {
        lv_label_set_text(cards[e.peer].name, radio::peer(e.peer)->hello.name);
      }
      break;
    case RadioEvt::ScanDone:
      lastScanEnd = millis();
      lv_bar_set_value(scanBar, 100, LV_ANIM_ON);
      nodesRebuild();
      break;
    case RadioEvt::Telemetry:
      if (e.peer == radio::selected()) powerOnTelemetry(radio::peer(e.peer));
      break;
    case RadioEvt::Anims:
      if (e.peer == radio::selected()) animsRebuild();
      break;
    case RadioEvt::Ack:
      if (e.status != PL_OK) toast(e.text && e.text[0] ? e.text : radio::statusName(e.status), C_RED);
      break;
    case RadioEvt::Timeout:
      if (e.cmd == PL_CMD) toast("NO ANSWER FROM NODE", C_AMBER);
      break;
    default:
      break;
  }
}

void termSink(const char *line, uint32_t color) {
  if (!built) {
    if (earlyLines.size() < 80) earlyLines.push_back({String(line), color});
    return;
  }
  termAdd(line, color);
}

void termClear() {
  if (built) lv_obj_clean(termOut);
}

}  // namespace ui
