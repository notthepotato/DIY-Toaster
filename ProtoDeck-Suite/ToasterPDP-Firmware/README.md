ToasterPDP firmware
-
Firmware for the XIAO ESP32-C6 that plugs into the PDP (JP4/JP5). Written against the 9/28/2026 schematic.

What it does
-
- Reads the **battery voltage** (VIN DIV), **battery-side current** (U1) and **5 V rail current** (U3)
- **Fuel gauge**: counts amp-hours out of the battery and slowly corrects itself against the battery voltage, then estimates **time remaining**
- **Warnings** for low/critical battery, over-voltage, input voltage too low for the 5 V regulator, over-current on either side, sensor near the ADC limit and chip over-temperature. It can only *warn*: nothing on the board lets the XIAO switch the outputs off
- **Onboard NeoPixel** as a status light, plus an optional strip on **P1** (any effect, or a battery bar)
- **FAN header** (P2): 25 kHz PWM fan control on D9, optional tach on D8 (JP11), manual or automatic speed
- **Console** on USB *and* on the J2 SERIAL header, plus a JSON telemetry stream on J2
- **ProtoDeck**: shows up as a `power` node, with live voltage/current/power, a battery bar and remote control of the fan and LEDs

Flashing
-
**Arduino IDE**
1. Boards Manager: **esp32 by Espressif** 3.x. Library Manager: **Adafruit NeoPixel**
2. Open `ToasterPDP/ToasterPDP.ino`
3. Tools > Board **XIAO_ESP32C6**, USB CDC On Boot **Enabled**
4. Upload, then open the Serial Monitor at **115200** with "Newline" line endings and type `help`

**PlatformIO**: open this `ToasterPDP-Firmware` folder and press Upload.

The ProtoLink library is already copied into `ToasterPDP/src/protolink`, so nothing else needs installing.

First-time setup (5 minutes, do it once)
-
1. **Battery type.** The default is two 3S "12 V" Li-ion packs in series (6S, 6000 mAh). Change it to match yours:
   ```
   bat li-ion 6 6000        two 3S Li-ion packs (25.2 V full)
   bat lifepo4 8 6000       two 4S LiFePO4 packs (12.8 V each)
   bat lead-acid 12 7000    two 12 V SLA blocks (6 cells each)
   ```
2. **Zero the current sensors.** Connect the battery, unplug everything from the 5 V and VCC headers, then run `cal zero`. The PDP's own ~70 mA is already accounted for.
3. **Battery voltage.** Measure the battery with a multimeter and type it in, e.g. `cal vin 24.31`.
4. **Optional current calibration.** Put a known steady load on, measure it with a meter in series, then run `cal out 2.05` (5 V side) and/or `cal in 0.48` (battery side).
5. After a full charge, run `bat full`.

Everything is saved in flash. `cal` shows the current calibration, and `cal reset` goes back to datasheet values.

Status LED (onboard NeoPixel)
-
| Looks like | Means |
|---|---|
| slow breathing, green > amber > red | battery level |
| same colour, slow blink | battery low (20 % or below) |
| fast red/amber flashing | critical: battery critical/over-voltage, over-current, sensor at the ADC limit, or too hot |
| blue breathing | no battery, running from USB |
| dim white | gauge still settling (first 1.5 s) |
| white strobe | identify (`id`, or the ID button on the deck) |
| quick cyan flick | a command arrived from the ProtoDeck |

The XIAO's own small yellow LED blinks every 2 s. A double blink means an alarm is active.

Console
-
The console works over USB, over J2 SERIAL (115200 8N1) and from the ProtoDeck terminal (`say <command>`).

```
status | s              full dashboard              json          one JSON line
watch [ms] | watch off  live one-line readout       stats         peaks, min voltage, energy used
stream <ms> | stream off  JSON lines on J2 SERIAL (saved; for another microcontroller)
bat [...]  bat soc <n>  bat full    rint <milliohm>  (pack resistance, used for voltage-sag compensation)
cal  cal zero  cal vin <V>  cal in <A>  cal out <A>  cal div in|out <ratio>  cal reset  raw
limit in|out <A>  limit temp <C>          (warning thresholds; defaults 7 A / 9 A / 75 C)
fan <0-100>  fan off  fan auto [min t0 t1]  fan
leds <count>  ledtest  fx <effect> [c1] [c2] [speed]  bri strip|status <0-255>  id
name <text>  ch <1-13>  i2c  save  defaults  reset stats  reboot  version
```
Effects: `gauge` (battery bar), `off solid breathe rainbow chase scanner sparkle gradient strobe plasma`.
Colours: `#RRGGBB` or `red green blue cyan pink purple white orange yellow`.

Example `status`:
```
ToasterPDP  v1.0.0  up 00:42:10  ESP-NOW ch 1  deck 1s ago
BATTERY    23.84 V  (3.973 V/cell, 6S li-ion)  77%  ~4h05m left
  draw      1.12 A    26.7 W      peak 3.40 A   min 23.10 V
5V RAIL     4.41 A    22.1 W      peak 6.02 A
  other   ~  2.7 W  (VCC/24 V loads + regulator loss)
FAN        55% (auto)  2140 rpm      CHIP 41.2 C
LEDS      status bri 40, 30 on P1 fx GAUGE bri 80 (~210 mA)
ENERGY    1410 mAh / 33.90 Wh used this session
ALARMS    none
```
The input sensor sits before the reverse-polarity FET, so "draw" covers everything: the 5 V buck *and* anything on the VCC (24 V) headers, such as the fans. "other" is that 24 V side plus regulator loss.

ProtoDeck
-
The PDP joins on ESP-NOW channel 1 (ProtoESP's default; change it with `ch`). Hit SCAN on the deck and it appears as **ToasterPDP**, kind `power`.

| Deck | On the PDP |
|---|---|
| POWER page | battery voltage, battery current, battery power, chip temperature, chart, **BATTERY** bar, `84% 3h12m 5V 4.4A` status chip |
| CONTROL page | VISOR slider = status LED brightness, EARS slider = strip brightness, FAN slider, ID, NATIVE (= battery bar), OFF (= strip off) |
| FX page | any preset with zone ALL or AUX goes to the P1 strip |
| terminal | `sel ToasterPDP`, `tele`, `fan 128`, `say status`, `say leds 30`, `say cal` ... |

Alarms are also pushed to the deck's terminal as they happen.

The deck needed two small changes for this: the BATTERY bar, and the voltage chart scaling past 6 V. They're already in `../ProtoDeck-Controller` in this same suite, so re-flash the deck too.

Pin map (from the schematic)
-
| Net | Header | XIAO | GPIO | Used for |
|---|---|---|---|---|
| VIN DIV | JP4-1 | D0 | 0 | battery voltage / 11 (R3 1M, R4 100k) |
| I SENSE | JP4-2 | D1 | 1 | U1 ACS724, battery side |
| I SENSE 1 | JP4-3 | D2 | 2 | U3 ACS724, 5 V rail |
| NEOPIXEL | JP4-4 | D3 | 21 | D2 WS2812B, then P1 DOUT |
| SDA / SCL | JP4-5/6 | D4/D5 | 22/23 | J1 I2C (10k pull-ups on board), `i2c` scans it |
| TX / RX | JP4-7 / JP5-7 | D6/D7 | 16/17 | J2 SERIAL |
| D8 | JP5-6 | D8 | 19 | fan tach (JP11-2), optional |
| D9 | JP5-5 | D9 | 20 | P2 FAN pin 2: PWM |
| D10 | JP5-4 | D10 | 18 | free (JP14-2) |

Hardware notes (worth fixing on the next revision)
-
1. **Current sensors into 3.3 V pins.** Both ACS724s are powered from 5 V, so their output is 0.5 V + 0.4 V/A, and that goes straight into the XIAO.
   - **5 V rail (U3):** at about **6.5 A** the reading hits the top of the ADC. Past roughly **7.5 A** the pin is over the ESP32-C6's maximum rating, and at the board's full 10 A it would see **4.5 V**.
   - **Battery side (U1):** stays well inside the limit in practice.

   The firmware warns you (fast-flashing LED, `OUT SENSOR NEAR ADC LIMIT`), but it can't protect the pin. The fix is a divider between each VOUT and the XIAO. 10k series plus 20k to GND gives 10 A = 3.0 V, and it can be bodged at JP4. Then tell the firmware: `cal div out 0.6667`, then `cal zero`.
2. **VIN DIV has no filter cap.** 1 MΩ/100 kΩ into the ESP32's ADC reads a little low and a little noisy. A 100 nF cap from VIN DIV to GND helps; `cal vin` corrects the rest.
3. **BOM vs schematic: C5B–C5F.** The schematic says 22 µF 1206. The BOM row says "22 uF (0603)" but its part number, CL31B106KLHNNNE, is a **10 µF** 1206 (the same part as C6D/C6E). That's less than half the intended output capacitance on a 10 A buck, so worth double-checking before you order.
4. **FAN header (P2)** is GND / D9 / 5 V. D9 is a signal, not power, so it drives the PWM wire of a 4-pin fan, with the fan's power coming from the 5 V pin. A 2- or 3-wire fan can't be speed-controlled from here; just power it from 5 V or VCC. D9 is set to open-drain, which is what 4-pin fans expect. Set `FAN_OPEN_DRAIN 0` in `config.h` if you drive a MOSFET gate instead.

Files
-
| File | What |
|---|---|
| `config.h` | pins, analog front-end constants, defaults |
| `sensors.*` | ADC sampling (8x oversampling every 10 ms), filtering, peaks |
| `power.*` + `pdp_math.h` | fuel gauge, battery curves (Li-ion / LiFePO4 / lead-acid), alarms |
| `fan.*` | 25 kHz PWM + tach |
| `leds.*` | status pixel + strip effects / battery bar |
| `cli.*` | console commands (USB, J2, ProtoDeck) |
| `pdplink.*` | ProtoLink/ESP-NOW node for the ProtoDeck |
| `settings.*` | everything saved in flash |

Status: compiles cleanly (`-Wall`) for the XIAO ESP32-C6 on arduino-esp32 3.3.12. The measurement, fuel-gauge, alarm, calibration and console code was also run on a PC against simulated sensor readings. It has **not** been run on a real PDP yet.
