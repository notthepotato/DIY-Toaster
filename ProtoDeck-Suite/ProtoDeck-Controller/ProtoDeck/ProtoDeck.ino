/* ===========================================================================
   ___  ___  ___ _____ ___    __ __  ___  ___ ___ _  __
  | _ \| _ \/ _ \_   _/ _ \  / // / |   \| __/ __| |/ /
  |  _/|   / (_) || || (_) |/ // /  | |) | _| (__| ' <
  |_|  |_|_\\___/ |_| \___//_//_/   |___/|___\___|_|\_\

  PROTO//DECK  -  CrowPanel Advance 7.0" HMI (ESP32-S3) controller for
  ProtoLink / ESP-NOW nodes: protogen helmets (ProtoESP), tails, ears...

  Board settings (Arduino IDE):  ESP32S3 Dev Module, Flash 16MB, PSRAM "OPI PSRAM",
  Partition "16M Flash (3MB APP/9.9MB FATFS)", USB CDC On Boot "Disabled".
  Libraries: lvgl 9.2.x (copy lv_conf.h next to the lvgl library folder).
=========================================================================== */
#include <Arduino.h>
#include <lvgl.h>
#include "board.h"
#include "config.h"
#include "console.h"
#include "presets.h"
#include "radio.h"
#include "ui.h"

static void onRadioEvent(const RadioEvent &e) {
  console::onRadio(e);
  bootseq::onRadio(e);
  ui::onRadio(e);
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\r\n" DECK_NAME " v" DECK_VERSION " booting...");

  settings::load();
  presets::begin();

  if (!board::begin()) Serial.println("[E] RGB panel init failed");
  board::setClicks(settings::get().clicks);

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  board::attachLvgl();

  console::begin();
  console::addSink(ui::termSink);
  console::setClear(ui::termClear);
  radio::addListener(onRadioEvent);

  ui::build();

  if (settings::get().skipBoot) {
    if (radio::begin()) radio::setChannel(settings::get().homeChannel);
    board::setBacklight(settings::get().backlight);
    ui::show(false);
    if (settings::get().scanOnBoot) radio::startScan();
  } else {
    bootseq::start([]() { ui::show(true); });
  }
  console::print(CON_DIM, "type 'help' for commands");
}

void loop() {
  radio::loop();
  console::loop();
  board::loop();
  lv_timer_handler();
  delay(2);
}
