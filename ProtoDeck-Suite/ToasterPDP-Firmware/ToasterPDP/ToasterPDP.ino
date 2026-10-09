/* ============================================================================
   ToasterPDP  -  firmware for the DIY-Toaster power distribution board
   (Seeed XIAO ESP32-C6 on "Proot test board", schematic rev 9/28/2026)

   - battery voltage, battery-side current and 5 V rail current (2x ACS724)
   - fuel gauge (coulomb counting + voltage curve) with time-remaining estimate
   - warnings for low/critical battery, over-current, over-temp, sensor limits
   - onboard NeoPixel as a status light, optional strip on P1 (effects or a
     battery bar), PWM fan on the FAN header with optional tach
   - console on USB and on the J2 SERIAL header, JSON telemetry stream
   - ProtoLink/ESP-NOW node: shows up on the ProtoDeck with live power data

   Arduino IDE: board "XIAO_ESP32C6" (esp32 by Espressif 3.x), USB CDC On Boot
   "Enabled", library "Adafruit NeoPixel". PlatformIO: see platformio.ini.
   ============================================================================ */
#include <Arduino.h>
#include <Wire.h>
#include "cli.h"
#include "config.h"
#include "fan.h"
#include "leds.h"
#include "pdplink.h"
#include "power.h"
#include "sensors.h"
#include "settings.h"

HardwareSerial ExtSerial(1);  // J2 "SERIAL" header (TX D6 / RX D7)

static void onAlarm(uint16_t bit, bool on) {
  if (bit == ALM_NO_BATTERY) {
    cli::broadcast(on ? "[i] no battery - running from USB" : "[i] battery connected");
    return;
  }
  int i = 0;
  while (i < 16 && !(bit & (1u << i))) i++;
  char line[96];
  const Readings &r = sensors::get();
  snprintf(line, sizeof(line), "[%s] %s  (%.2f V, in %.2f A, 5V %.2f A, %.0f%%)", on ? "!" : "ok", power::alarmName(i),
           r.vin, r.iin, r.iout, power::socPct());
  cli::broadcast(line);
  pdplink::log("%s", line);
}

void setup() {
  Serial.begin(CONSOLE_BAUD);
  ExtSerial.begin(CONSOLE_BAUD, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
  pinMode(PIN_HEARTBEAT, OUTPUT);
  digitalWrite(PIN_HEARTBEAT, HIGH);  // off (active low)

  settings::load();
  Wire.begin(PIN_SDA, PIN_SCL, 400000);

  sensors::begin();
  power::begin();
  power::onAlarm(onAlarm);
  fan::begin();
  leds::begin();
  cli::begin(Serial, ExtSerial);
  pdplink::begin();

  delay(200);  // give USB a moment so the banner isn't lost
  cli::broadcast("");
  cli::broadcast("ToasterPDP v" PDP_FW_VERSION " - type 'help'");
  cli::status(Serial);
}

void loop() {
  if (sensors::update()) power::update();
  fan::update();
  leds::update();
  pdplink::loop();
  cli::loop();

  // heartbeat on the XIAO's own LED: short blip every 2 s (double blip if an alarm is active)
  uint32_t t = millis() % 2000;
  bool alarm = power::alarms() & ~ALM_NO_BATTERY;
  bool on = t < 40 || (alarm && t >= 160 && t < 200);
  digitalWrite(PIN_HEARTBEAT, on ? LOW : HIGH);
  delay(1);
}
