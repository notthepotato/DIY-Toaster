// ============================================================================
//  ui.h  -  ProtoDeck touch UI (LVGL 9) + boot sequence
// ============================================================================
#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include "radio.h"

// JetBrains Mono (SIL OFL 1.1), generated for LVGL - see font_mono_*.c
LV_FONT_DECLARE(font_mono_12)
LV_FONT_DECLARE(font_mono_14)
LV_FONT_DECLARE(font_mono_16)
LV_FONT_DECLARE(font_mono_30)

// palette (0xRRGGBB)
#define C_BG     0x04070B
#define C_PANEL  0x0A1118
#define C_PANEL2 0x0F1A24
#define C_LINE   0x1B2B38
#define C_CYAN   0x22E4FF
#define C_PINK   0xFF3D8B
#define C_AMBER  0xFFB020
#define C_GREEN  0x3DFFA2
#define C_RED    0xFF4D5E
#define C_TEXT   0xCFE8F2
#define C_DIM    0x5D7A8A

namespace ui {
void build();                                   // create the main screen (not shown yet)
void show(bool animated = true);                // load it
void onRadio(const RadioEvent &e);              // radio listener
void termSink(const char *line, uint32_t color); // console sink
void termClear();
void toast(const char *text, uint32_t color = C_CYAN);
}  // namespace ui

namespace bootseq {
typedef void (*DoneFn)();
void start(DoneFn done);                         // runs on its own screen, then calls done()
bool running();
void onRadio(const RadioEvent &e);
}  // namespace bootseq
