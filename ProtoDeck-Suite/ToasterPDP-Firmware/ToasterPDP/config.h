// ============================================================================
//  ToasterPDP - compile-time configuration
//  Board: DIY-Toaster "Proot test board" PDP (LM61495 5 V/10 A buck, LM74700
//  reverse-voltage protection, 2x ACS724LLCTR-10AU current sensors) with a
//  Seeed XIAO ESP32-C6 plugged into JP4/JP5.
//
//  Most of these are only DEFAULTS: battery type, calibration, limits, fan and
//  LED settings can all be changed at runtime from the console and are saved
//  in flash. Change things here only if your hardware differs.
// ============================================================================
#pragma once
#include <Arduino.h>

#define PDP_FW_VERSION   "1.0.0"
#define PDP_DEFAULT_NAME "ToasterPDP"     // shown on the ProtoDeck (max 19 chars)

// ---- XIAO ESP32-C6 pins (schematic net -> XIAO pin -> GPIO) ----------------
#define PIN_VIN_DIV      D0   // GPIO0  JP4-1  "VIN DIV"    battery voltage / 11
#define PIN_ISENSE_IN    D1   // GPIO1  JP4-2  "I SENSE"    U1 ACS724, battery side
#define PIN_ISENSE_OUT   D2   // GPIO2  JP4-3  "I SENSE 1"  U3 ACS724, 5 V rail
#define PIN_NEOPIXEL     D3   // GPIO21 JP4-4  onboard WS2812B (D2) -> P1 "NEOPIXEL" DOUT
#define PIN_SDA          D4   // GPIO22 JP4-5  J1 "I2C" (10k pull-ups R7/R8 on board)
#define PIN_SCL          D5   // GPIO23 JP4-6
#define PIN_UART_TX      D6   // GPIO16 JP4-7  J2 "SERIAL" pin 3
#define PIN_UART_RX      D7   // GPIO17 JP5-7  J2 "SERIAL" pin 4
#define PIN_FAN_PWM      D9   // GPIO20 JP5-5  P2 "FAN" pin 2 (also JP11-1)
#define PIN_FAN_TACH     D8   // GPIO19 JP5-6  JP11-2 - optional tach input, -1 to disable
#define PIN_SPARE        D10  // GPIO18 JP5-4  JP14-2, unused
#define PIN_HEARTBEAT    LED_BUILTIN  // XIAO's own yellow LED (GPIO15, active LOW)

// ---- Analog front end ---------------------------------------------------------
// VIN DIV = VCC * R4 / (R3 + R4) = VCC * 100k / 1.1M
#define VIN_DIVIDER          11.0f
// ACS724LLCTR-10AU at VCC = 5 V: 0.5 V at 0 A (VCC/10), 400 mV per amp.
#define ACS_ZERO_MV          500.0f
#define ACS_MV_PER_A         400.0f
// If you add a resistor divider between a sensor's VOUT and the XIAO (strongly
// recommended for the 5 V-rail sensor, see README), put its ratio here
// (e.g. 10k top / 20k bottom = 0.6667). Also settable at runtime: "cal div out 0.6667".
#define ISENSE_IN_FRONTEND   1.0f
#define ISENSE_OUT_FRONTEND  1.0f
// A sensor pin reading at or above this is flagged as "near the ADC limit".
// Above ~3.6 V the ESP32-C6 pin itself is out of spec.
#define ADC_LIMIT_MV         3100.0f
// The XIAO's own 5 V draw, which the output sensor always sees (used by "cal zero").
#define SELF_CURRENT_A       0.07f
#define VOUT_NOMINAL         5.0f

// ---- Battery defaults (two "12 V" packs in series) ---------------------------
// chemistry: 0 = Li-ion/LiPo (3.0-4.2 V/cell), 1 = LiFePO4, 2 = lead-acid (2 V cells)
#define BAT_DEFAULT_CHEM     0
#define BAT_DEFAULT_CELLS    6        // 2 x 3S "12 V" Li-ion packs
#define BAT_DEFAULT_MAH      6000     // capacity of the series string
#define BAT_DEFAULT_RINT_MO  150      // pack internal resistance, milliohms (sag compensation)

// ---- Default limits (warnings only: the MCU cannot switch the outputs) --------
#define LIMIT_IN_A           7.0f     // battery-side sensor tops out at ~7 A on the ADC anyway
#define LIMIT_OUT_A          9.0f     // 5 V rail (regulator is rated 10 A)
#define LIMIT_TEMP_C         75.0f    // ESP32-C6 die temperature

// ---- Fan (P2 "FAN": pin1 GND, pin2 D9 control, pin3 +5 V) ---------------------
// D9 drives the PWM wire of a 4-pin-style fan (25 kHz, open-drain as the Intel
// spec expects; the fan pulls the line up itself). A 2/3-wire fan can't be
// speed-controlled from this header - power those from 5 V/VCC directly.
#define FAN_PWM_HZ           25000
#define FAN_OPEN_DRAIN       1
#define FAN_TACH_PULSES_REV  2

// ---- NeoPixels ------------------------------------------------------------------
// Pixel 0 is the onboard status LED (D2). Extra pixels chained on P1 continue
// from index 1. "leds <n>" sets how many are on P1 (saved).
#define LED_MAX_EXTERNAL     300
#define LED_DEFAULT_EXTERNAL 0
#define LED_STATUS_BRIGHT    40       // onboard status LED (0-255)

// ---- Links ----------------------------------------------------------------------
#define CONSOLE_BAUD         115200   // USB (XIAO native USB) and J2 SERIAL
#define ESPNOW_CHANNEL       1        // must match your helmet's soft-AP channel (ProtoESP default 1)

// ---- Timing ---------------------------------------------------------------------
#define SAMPLE_PERIOD_MS     10       // ADC block every 10 ms
#define SAMPLES_PER_BLOCK    8        // oversampling per channel per block
#define SOC_SAVE_PERIOD_MS   (5UL * 60UL * 1000UL)
