// ============================================================================
//  console.h  -  one command language for the USB serial terminal AND the
//  on-screen terminal. UI buttons also go through exec(), so every action
//  shows up in the terminal log.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <vector>
#include "radio.h"

// terminal colours (0xRRGGBB)
#define CON_TEXT    0xCFE8F2
#define CON_DIM     0x5D7A8A
#define CON_OK      0x3DFFA2
#define CON_WARN    0xFFB020
#define CON_ERR     0xFF4D5E
#define CON_ACCENT  0x22E4FF
#define CON_PINK    0xFF3D8B

namespace console {

typedef void (*Sink)(const char *line, uint32_t color);
typedef void (*ClearFn)();

void begin();
void loop();                                   // reads lines from Serial
void exec(const String &line, bool echo = true);
void print(uint32_t color, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void addSink(Sink s);
void setClear(ClearFn f);
void onRadio(const RadioEvent &e);             // register with radio::addListener
const std::vector<String> &history();
void neofetch();

}  // namespace console
