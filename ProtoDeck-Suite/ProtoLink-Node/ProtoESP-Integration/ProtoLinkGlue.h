// ============================================================================
//  ProtoLinkGlue.h  -  ProtoDeck <-> ProtoESP (NCPlyn/ProtogenHelmet-ESP32)
//
//  #include this ONCE in ProtoESP-Controller/src/main.cpp directly ABOVE
//  `void loop() {` (it needs the globals declared above that line).
//  See protoesp-main.patch / README for the 5 small edits.
//
//  What it adds:
//   * ESP-NOW node named after your helmet's WiFi name (cfg.wifiName)
//   * telemetry: INA219 voltage/current (or an LED-based estimate), chip temp,
//     mic level, brightness, fan, current animation, boop/tilt flags
//   * commands: play/next/prev animation, visor & ear brightness, fan,
//     visor mode (custom/rainbow), LED effects on the ears, visor colour tint,
//     identify, save config, reboot
//   * the same text commands the BLE remote uses ("?", ";rgb", anim name)
// ============================================================================
#pragma once
#include "ProtoLinkNode.h"
#include "pl_effects.h"

static PlLedFx  plEarFx       = {PL_ZONE_EARS, PL_FX_ANIM, 0, 128, 0, 0, 0, 0, 0, 0};
static bool     plVisorTintOn = false;
static uint32_t plVisorTint   = 0;
static uint32_t plIdentifyEnd = 0;
static int      plVisorBriBackup = -1;   // restored after "OFF" / identify

// Used by the (optional, patched) setAllVisor() call: 0 = use animation colours.
uint32_t protoLinkVisorColor() { return plVisorTintOn ? (plVisorTint ? plVisorTint : 0x010101) : 0; }

static String plStripExt(const String &f) { return f.endsWith(".json") ? f.substring(0, f.length() - 5) : f; }

static int plFindAnim(const char *name) {
  for (int i = 0; i < totalAnims; i++)
    if (plStripExt(availAnims[i]).equalsIgnoreCase(name)) return i;
  return -1;
}

static int plCurrentAnimIndex() {
  for (int i = 0; i < totalAnims; i++)
    if (availAnims[i] == currentAnim) return i;
  return -1;
}

static void plForceAnimReload() {  // re-read the current animation (restores its ear/visor types)
  if (currentAnim.length()) {
    String a = currentAnim;
    currentAnim = "";
    animToLoad = a;
  }
}

static bool plIsWS2812Visor() { return strcmp(visorType, "WS2812") == 0; }

static void plSetVisorType(uint8_t t) {
  if (!plIsWS2812Visor()) return;
  visorNow->type = t % visTypeSize;
  if (cfg.oledEna && oledInitDone) oled.writeRGB(vTAcro[visorNow->type]);
}

static uint8_t plApplyFx(const PlLedFx &f, char *msg, size_t ml) {
  bool visor = f.zone == PL_ZONE_ALL || f.zone == PL_ZONE_VISOR;
  bool ears  = f.zone == PL_ZONE_ALL || f.zone == PL_ZONE_EARS;
  if (f.zone == PL_ZONE_AUX) return PL_ERR_UNSUPPORTED;
  String done;

  if (visor) {
    if (plVisorBriBackup >= 0 && f.fx != PL_FX_OFF) { cfg.bVisor = plVisorBriBackup; plVisorBriBackup = -1; }
    switch (f.fx) {
      case PL_FX_ANIM:
        plVisorTintOn = false;
        plSetVisorType(PL_VISOR_CUSTOM);
        break;
      case PL_FX_SOLID:
        plVisorTint = ((uint32_t)f.r1 << 16) | ((uint32_t)f.g1 << 8) | f.b1;
        plVisorTintOn = true;
        cfg.visColor = plVisorTint;  // also the default colour for plain animations
        plSetVisorType(PL_VISOR_CUSTOM);
        break;
      case PL_FX_RAINBOW:
        plVisorTintOn = false;
        cfg.rbSpeed = map(f.speed, 0, 255, 40, 2);  // ProtoESP: smaller = faster
        plSetVisorType(PL_VISOR_RAINBOW);
        break;
      case PL_FX_OFF:
        if (plVisorBriBackup < 0) plVisorBriBackup = cfg.bVisor;
        cfg.bVisor = 0;
        break;
      default:
        if (f.zone == PL_ZONE_VISOR) {
          snprintf(msg, ml, "visor: ANIM/SOLID/RAINBOW/OFF only");
          return PL_ERR_UNSUPPORTED;
        }
        break;  // zone ALL: visor keeps its animation, ears get the effect
    }
    if (f.brightness && f.fx != PL_FX_OFF) cfg.bVisor = f.brightness;
    instantReload = true;
    FdisplayVisor = true;
    done += "visor ";
  }

  if (ears) {
    if (!earPresent) {
      if (f.zone == PL_ZONE_EARS) { snprintf(msg, ml, "no ear LEDs configured"); return PL_ERR_UNSUPPORTED; }
    } else {
      if (f.fx == PL_FX_ANIM) {
        if (plEarFx.fx != PL_FX_ANIM) plForceAnimReload();
        plEarFx.fx = PL_FX_ANIM;
      } else {
        plEarFx = f;
        earsNow->type = 5;  // "none": ProtoESP stops drawing ears, we draw them
      }
      if (f.brightness) cfg.bEar = f.brightness;
      FdisplayEar = true;
      done += "ears ";
    }
  }
  snprintf(msg, ml, "%s%s", done.c_str(), plFxName(f.fx));
  return PL_OK;
}

class ProtoEspDevice : public ProtoLinkDevice {
 public:
  void fillTelemetry(PlTelemetry &t) override {
    if (INApresent) {
      t.busV = ina219.getBusVoltage_V();
      t.currentmA = ina219.getCurrent_mA();
      t.powermW = t.busV * t.currentmA;
    } else {  // estimate from what the LEDs are currently showing
      float est = 120.0f;  // ESP32-S3 + radio
      if (plIsWS2812Visor()) est += plfx::estimateCurrentmA((uint8_t *)visorLeds, visorLedsNum, cfg.bVisor);
      if (earPresent) est += plfx::estimateCurrentmA((uint8_t *)earLeds, earLedsNum, cfg.bEar);
      if (blushPresent) est += plfx::estimateCurrentmA((uint8_t *)blushLeds, blushLedsNum, cfg.bEar);
      t.currentmA = est;
      t.powermW = est * 5.0f;
      t.flags |= PL_TF_ESTIMATED;
    }
    t.tempC = temperatureRead();
    t.fanDuty = cfg.fanDuty;
    t.brightVisor = cfg.bVisor > 255 ? 255 : cfg.bVisor;
    t.brightEars = cfg.bEar > 255 ? 255 : cfg.bEar;
    t.micLevel = (uint8_t)constrain(micVolume, 0, 100);
    if (booping) t.flags |= PL_TF_BOOPED;
    if (wasTilt) t.flags |= PL_TF_TILTED;
    if (cfg.speechEna) t.flags |= PL_TF_SPEECH;
    if (cfg.speechEna && micVolume > cfg.spTrig) t.flags |= PL_TF_TALKING;
    t.fx = plEarFx.fx;
    snprintf(t.anim, sizeof(t.anim), "%s", plStripExt(currentAnim).c_str());
  }

  uint16_t animCount() override { return totalAnims; }
  const char *animName(uint16_t i) override {
    static char buf[40];
    if (i >= totalAnims) return nullptr;
    snprintf(buf, sizeof(buf), "%s", plStripExt(availAnims[i]).c_str());
    return buf;
  }

  uint8_t onCommand(uint8_t id, const uint8_t *a, uint8_t len, char *msg, size_t ml) override {
    switch (id) {
      case PL_CMD_SET_ANIM: {
        char name[40] = {0};
        memcpy(name, a, len < sizeof(name) - 1 ? len : sizeof(name) - 1);
        int i = plFindAnim(name);
        if (i < 0) { snprintf(msg, ml, "no anim '%s'", name); return PL_ERR_ARG; }
        animToLoad = availAnims[i];
        snprintf(msg, ml, "playing %s", name);
        return PL_OK;
      }
      case PL_CMD_NEXT_ANIM:
      case PL_CMD_PREV_ANIM: {
        if (!totalAnims) return PL_ERR_UNSUPPORTED;
        int i = plCurrentAnimIndex();
        i = (id == PL_CMD_NEXT_ANIM) ? i + 1 : i - 1;
        if (i >= totalAnims) i = 0;
        if (i < 0) i = totalAnims - 1;
        animToLoad = availAnims[i];
        snprintf(msg, ml, "playing %s", plStripExt(availAnims[i]).c_str());
        return PL_OK;
      }
      case PL_CMD_BRIGHTNESS: {
        if (len < 2) return PL_ERR_ARG;
        uint8_t z = a[0], v = a[1];
        if (z == PL_ZONE_ALL || z == PL_ZONE_VISOR) cfg.bVisor = plIsWS2812Visor() ? v : map(v, 0, 255, 0, 15);
        if (z == PL_ZONE_ALL || z == PL_ZONE_EARS) cfg.bEar = v;
        plVisorBriBackup = -1;
        FdisplayVisor = FdisplayEar = FdisplayBlush = true;
        snprintf(msg, ml, "%s brightness %u", plZoneName(z), v);
        return PL_OK;
      }
      case PL_CMD_FAN:
        if (len < 1) return PL_ERR_ARG;
        cfg.fanDuty = a[0];
        ledcWrite(fanPWM, cfg.fanDuty);  // Arduino 3.x API, same as ProtoESP
        snprintf(msg, ml, "fan %u", a[0]);
        return PL_OK;
      case PL_CMD_VISOR_MODE:
        if (!plIsWS2812Visor()) return PL_ERR_UNSUPPORTED;
        plVisorTintOn = false;
        if (len < 1 || a[0] == PL_VISOR_TOGGLE) plSetVisorType(visorNow->type + 1);
        else plSetVisorType(a[0]);
        snprintf(msg, ml, "visor %s", visorTypes[visorNow->type].c_str());
        return PL_OK;
      case PL_CMD_LED_FX: {
        if (len < sizeof(PlLedFx)) return PL_ERR_ARG;
        PlLedFx f;
        memcpy(&f, a, sizeof(f));
        return plApplyFx(f, msg, ml);
      }
      case PL_CMD_IDENTIFY:
        plIdentifyEnd = millis() + 1600;
        if (plVisorBriBackup < 0) plVisorBriBackup = cfg.bVisor;
        snprintf(msg, ml, "blinking visor");
        return PL_OK;
      case PL_CMD_SAVE:
        if (plVisorBriBackup >= 0) { cfg.bVisor = plVisorBriBackup; plVisorBriBackup = -1; }
        snprintf(msg, ml, cfg.save() ? "config saved" : "save failed");
        return PL_OK;
      default:
        return PL_ERR_UNSUPPORTED;
    }
  }

  // Same vocabulary as the BLE remote, plus a few extras.
  void onText(const char *text, char *reply, size_t rl) override {
    String t = String(text);
    t.trim();
    if (t == "?" || t == "anims") {
      String s;
      for (int i = 0; i < totalAnims; i++) s += plStripExt(availAnims[i]) + ";";
      snprintf(reply, rl, "%s", s.c_str());
    } else if (t == ";rgb" || t == "rgb") {
      plSetVisorType(visorNow->type + 1);
      snprintf(reply, rl, "visor %s", visorTypes[visorNow->type].c_str());
    } else if (t == "help") {
      snprintf(reply, rl,
               "?/anims, <anim name>, rgb, status, log, fan <0-255>, bvisor <0-255>, bear <0-255>, heap");
    } else if (t == "status") {
      snprintf(reply, rl, "anim=%s visor=%s bV=%d bE=%d fan=%d ina=%s boop=%s tilt=%s speech=%s",
               plStripExt(currentAnim).c_str(), visorTypes[visorNow->type].c_str(), cfg.bVisor, cfg.bEar,
               cfg.fanDuty, INApresent ? "yes" : "no", cfg.boopEna ? "on" : "off", cfg.tiltEna ? "on" : "off",
               cfg.speechEna ? "on" : "off");
    } else if (t == "heap") {
      snprintf(reply, rl, "heap %u, psram %u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
    } else if (t == "log") {  // tail of ProtoESP's realtime log
      if (logBuffer && logIndex) {
        size_t from = logIndex > rl - 1 ? logIndex - (rl - 1) : 0;
        snprintf(reply, rl, "%s", logBuffer + from);
      } else snprintf(reply, rl, "(log empty)");
    } else if (t.startsWith("fan ")) {
      cfg.fanDuty = constrain(t.substring(4).toInt(), 0, 255);
      ledcWrite(fanPWM, cfg.fanDuty);
      snprintf(reply, rl, "fan %d", cfg.fanDuty);
    } else if (t.startsWith("bvisor ")) {
      cfg.bVisor = constrain(t.substring(7).toInt(), 0, 255);
      FdisplayVisor = true;
      snprintf(reply, rl, "visor brightness %d", cfg.bVisor);
    } else if (t.startsWith("bear ")) {
      cfg.bEar = constrain(t.substring(5).toInt(), 0, 255);
      FdisplayEar = true;
      snprintf(reply, rl, "ear brightness %d", cfg.bEar);
    } else {
      int i = plFindAnim(t.c_str());
      if (i >= 0) {
        animToLoad = availAnims[i];
        snprintf(reply, rl, "playing %s", t.c_str());
      } else {
        snprintf(reply, rl, "unknown '%s' (try help)", t.c_str());
      }
    }
  }
};

static ProtoEspDevice plDevice;

// Call once in setup(), AFTER startWiFiWeb() and getFilesFunc().
void protoLinkBegin() {
  ProtoLinkConfig c;
  static char name[20];
  snprintf(name, sizeof(name), "%s", cfg.wifiName.c_str());
  c.name = name;
  c.kind = "protogen";
  c.caps = PL_CAP_ANIMS | PL_CAP_LEDFX | PL_CAP_POWER | PL_CAP_FAN | PL_CAP_VISOR | PL_CAP_TEXT | PL_CAP_TEMP |
           (earPresent ? PL_CAP_EARS : 0) | (cfg.speechEna ? PL_CAP_MIC : 0);
  c.ledCount = (plIsWS2812Visor() ? visorLedsNum : 0) + (earPresent ? earLedsNum : 0) +
               (blushPresent ? blushLedsNum : 0);
  if (ProtoLink.begin(c, &plDevice)) logPrint(F("[I] ProtoLink (ESP-NOW) ready"));
  else logPrint(F("[E] ProtoLink (ESP-NOW) failed to start"));
}

// Call at the very top of loop().
void protoLinkLoop() {
  ProtoLink.loop();
  uint32_t now = millis();

  // identify: blink the visor brightness for a moment
  if (plIdentifyEnd) {
    if ((int32_t)(plIdentifyEnd - now) > 0) {
      static uint32_t lastBlink = 0;
      if (now - lastBlink > 150) {
        lastBlink = now;
        cfg.bVisor = cfg.bVisor ? 0 : (plVisorBriBackup > 0 ? plVisorBriBackup : 100);
        FdisplayVisor = true;
      }
    } else {
      plIdentifyEnd = 0;
      if (plVisorBriBackup >= 0) { cfg.bVisor = plVisorBriBackup; plVisorBriBackup = -1; }
      FdisplayVisor = true;
    }
  }

  // ear effect override (sticky across animation changes)
  if (earPresent && plEarFx.fx != PL_FX_ANIM) {
    if (earsNow->type != 5) earsNow->type = 5;  // a new animation was loaded: keep overriding
    static uint32_t lastEar = 0;
    if (now - lastEar >= 16) {
      lastEar = now;
      plfx::render(plEarFx, (uint8_t *)earLeds, earLedsNum, now);
      FdisplayEar = true;
    }
  }
}
