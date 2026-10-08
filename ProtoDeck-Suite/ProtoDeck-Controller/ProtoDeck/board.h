// ============================================================================
//  board.h  -  Elecrow CrowPanel Advance 7.0" (ESP32-S3-WROOM-1-N16R8) HAL
//  800x480 RGB565 parallel panel, GT911 touch, STC8 helper MCU / IO expander.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <lvgl.h>

namespace board {

struct ProbeReport {
  bool helperMcu;  // STC8 @0x30 (V1.2+)
  bool ioExpander; // TCA9534/PCA9557 @0x18 (V1.0)
  bool touch;      // GT911 @0x5D/0x14
  bool rtc;        // PCF8563 @0x51
  uint8_t touchAddr;
};

bool begin();                 // I2C, helper MCU, touch reset, RGB panel (screen stays dark)
void attachLvgl();            // registers lv_display + touch lv_indev (call after lv_init)
void loop();                  // housekeeping (buzzer timing)

uint8_t revision();           // 10 / 12 / 13
const ProbeReport &probe();
bool i2cProbe(uint8_t addr);

void    setBacklight(uint8_t pct);  // 0..100 (0 = off)
uint8_t backlight();
void    sleep();                     // backlight off; next touch wakes (and is swallowed)
bool    asleep();
void    wake();

void beep(uint16_t ms = 10);         // V1.3+ onboard buzzer, non-blocking
void setClicks(bool on);             // UI click sound on touch press
bool clicks();

}  // namespace board
