# Adding ProtoLink to ProtoESP (your helmet)

Tested against [NCPlyn/ProtogenHelmet-ESP32](https://github.com/NCPlyn/ProtogenHelmet-ESP32) commit `101b4d7`
(Sep 2026). Nothing about the helmet changes except that it now also answers the ProtoDeck over ESP-NOW.

## Steps

1. Copy the whole `ProtoLink-Node` folder to `ProtoESP-Controller/lib/ProtoLinkNode/`
   (PlatformIO picks up anything in `lib/` automatically).
2. Copy `ProtoLinkGlue.h` to `ProtoESP-Controller/src/`.
3. Apply the patch from the repository root:
   ```
   git apply path/to/protoesp-main.patch
   ```
   or make the five edits by hand in `ProtoESP-Controller/src/main.cpp`:

   | Where | Add / change |
   |---|---|
   | after `Adafruit_VL53L1X vl53;` | `#include "ProtoLinkNode.h"` then `void protoLinkBegin();` `void protoLinkLoop();` `uint32_t protoLinkVisorColor();` |
   | in `setup()`, right after `getFilesFunc();` | `protoLinkBegin();` |
   | directly above `void loop() {` | `#include "ProtoLinkGlue.h"` |
   | first line inside `loop()`, after `ElegantOTA.loop();` | `protoLinkLoop();` |
   | in the visor "custom" branch | `setAllVisor(visorLedsNEW,0,currentVisorFrame);` -> `setAllVisor(visorLedsNEW,protoLinkVisorColor(),currentVisorFrame);` |

   The last edit is what lets a SOLID preset tint the face even when an animation has its own colours.
   It is a no-op until the deck sends a tint.
4. Build and upload as usual (filesystem image unchanged).

On boot the helmet log shows `[I] ProtoLink (ESP-NOW) ready`. The node is named after your helmet's WiFi
name (`cfg.wifiName`), so it shows up on the deck as e.g. **ProtoWiFi**.

## What the deck can read

| Field | Source |
|---|---|
| bus voltage, current, power | INA219 when `INApresent = true`; otherwise an estimate from the LED buffers (shown as **EST**) |
| chip temperature | ESP32-S3 internal sensor |
| animation, visor/ear brightness, fan duty | ProtoESP config/state |
| voice level, talking, boop, tilt | mic envelope, `booping`, `wasTilt`, speech settings |

## What the deck can do

play / next / prev animation, visor & ear brightness, fan duty, visor CUSTOM/RAINBOW, ear LED effects
(all ProtoLink effects), visor tint / rainbow / off / native, identify (blinks the visor), save config,
reboot, plus the BLE remote's text commands through `say` on the deck terminal:
`?` (anim list), `rgb`, an animation name, and extras `status`, `log`, `heap`, `fan N`, `bvisor N`, `bear N`.

## Notes

- **Channel**: ESP-NOW follows the helmet's soft-AP channel (1 unless you changed it). The deck finds it
  either way; set the deck's HOME CHANNEL (SYS page) to match for the fastest start-up.
- **Coexistence**: BLE remote, WiFi page, OTA and ESP-NOW all run together. Under heavy BLE traffic an
  occasional packet can be lost; the deck retries once.
- **Building on Linux**: upstream ProtoESP includes `"Misc.h"` and `<u8g2lib.h>`, while the files are
  `misc.h` / `U8g2lib.h`. That only breaks on case-sensitive filesystems (Linux); Windows and macOS are fine.
- **Licence**: `ProtoLinkGlue.h` is built into a GPL-3.0 program and is offered under GPL-3.0 to match.
  Keep NCPlyn's credits in the web pages, as the project asks.
