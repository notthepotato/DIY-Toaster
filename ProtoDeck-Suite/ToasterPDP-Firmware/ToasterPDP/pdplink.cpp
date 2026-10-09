#include "pdplink.h"
#include <stdarg.h>
#include "cli.h"
#include "config.h"
#include "fan.h"
#include "leds.h"
#include "power.h"
#include "sensors.h"
#include "settings.h"
#include "src/protolink/ProtoLinkNode.h"

namespace pdplink {
namespace {

class PdpDevice : public ProtoLinkDevice {
 public:
  void fillTelemetry(PlTelemetry &t) override {
    const Readings &r = sensors::get();
    const Settings &s = settings::get();
    if (r.batteryPresent) {
      t.busV = r.vin;
      t.currentmA = r.iin * 1000.0f;
      t.powermW = r.pin * 1000.0f;
    }
    t.tempC = r.tempC;
    t.fanDuty = (uint8_t)(fan::dutyPct() * 255 / 100);
    t.brightVisor = s.statusBright;
    t.brightEars = s.strip.brightness;
    float soc = power::socPct();
    t.micLevel = soc < 0 ? 0 : (uint8_t)(soc + 0.5f);  // ProtoDeck shows this as BATTERY for kind "power"
    t.fx = s.strip.fx;
    // free-text status line (shown as a chip on the deck's POWER page)
    if (power::alarms() & ALM_BAT_CRIT) snprintf(t.anim, sizeof(t.anim), "!! BATTERY %d%%", (int)(soc + 0.5f));
    else if (power::alarms() & ALM_OUT_LIMIT) snprintf(t.anim, sizeof(t.anim), "!! 5V SENSE LIMIT");
    else if (!r.batteryPresent) snprintf(t.anim, sizeof(t.anim), "USB 5V %.2fA", r.iout);
    else if (soc < 0) snprintf(t.anim, sizeof(t.anim), "GAUGE... 5V %.2fA", r.iout);
    else {
      char line[48];  // e.g. "84% 3h12m 5V 2.3A" (fits the 23-char field)
      snprintf(line, sizeof(line), "%d%% %s 5V %.1fA", (int)(soc + 0.5f), power::runtimeStr().c_str(), r.iout);
      strncpy(t.anim, line, sizeof(t.anim) - 1);
      t.anim[sizeof(t.anim) - 1] = 0;
    }
  }

  uint8_t onCommand(uint8_t id, const uint8_t *a, uint8_t len, char *msg, size_t ml) override {
    Settings &s = settings::get();
    leds::flick();
    switch (id) {
      case PL_CMD_BRIGHTNESS:
        if (len < 2) return PL_ERR_ARG;
        if (a[0] == PL_ZONE_VISOR) s.statusBright = a[1];
        else s.strip.brightness = a[1];
        snprintf(msg, ml, "%s brightness %u", a[0] == PL_ZONE_VISOR ? "status LED" : "strip", a[1]);
        return PL_OK;
      case PL_CMD_FAN:
        if (len < 1) return PL_ERR_ARG;
        s.fanMode = FAN_MANUAL;
        s.fanDuty = (uint8_t)((a[0] * 100 + 127) / 255);
        snprintf(msg, ml, "fan %u%%", s.fanDuty);
        return PL_OK;
      case PL_CMD_LED_FX: {
        if (len < sizeof(PlLedFx)) return PL_ERR_ARG;
        PlLedFx f;
        memcpy(&f, a, sizeof(f));
        if (f.zone == PL_ZONE_VISOR) {
          snprintf(msg, ml, "no visor here - use AUX/ALL");
          return PL_ERR_UNSUPPORTED;
        }
        uint8_t keepBri = s.strip.brightness;
        s.strip = f;
        if (!f.brightness) s.strip.brightness = keepBri;  // 0 = keep current brightness
        snprintf(msg, ml, "strip %s%s", plFxName(f.fx), f.fx == PL_FX_ANIM ? " (battery gauge)" : "");
        if (!s.ledCount) strncat(msg, " - 0 LEDs set, try: leds <n>", ml - strlen(msg) - 1);
        return PL_OK;
      }
      case PL_CMD_IDENTIFY:
        leds::identify(2000);
        snprintf(msg, ml, "blinking status LED");
        return PL_OK;
      case PL_CMD_SAVE:
        settings::save();
        power::saveSoc();
        snprintf(msg, ml, "settings saved");
        return PL_OK;
      case PL_CMD_CUSTOM: {
        char cmd[PL_MAX_PAYLOAD + 1];
        size_t n = len < sizeof(cmd) - 1 ? len : sizeof(cmd) - 1;
        memcpy(cmd, a, n);
        cmd[n] = 0;
        BufPrint bp(msg, ml);
        cli::exec(String(cmd), bp);
        return PL_OK;
      }
      default:
        snprintf(msg, ml, "not supported on the PDP");
        return PL_ERR_UNSUPPORTED;
    }
  }

  void onText(const char *text, char *reply, size_t rl) override {
    leds::flick();
    BufPrint bp(reply, rl);
    cli::exec(String(text), bp);
  }
} device;

}  // namespace

void begin() {
  const Settings &s = settings::get();
  ProtoLinkConfig c;
  c.name = s.name;
  c.kind = "power";
  c.caps = PL_CAP_POWER | PL_CAP_FAN | PL_CAP_LEDFX | PL_CAP_TEXT | PL_CAP_TEMP;
  c.ledCount = s.ledCount;
  c.channel = s.channel;
  c.fwMajor = 1;
  c.fwMinor = 0;
  ProtoLink.begin(c, &device);
}

void loop() { ProtoLink.loop(); }
bool started() { return ProtoLink.started(); }
bool deckKnown() { return ProtoLink.controllerKnown(); }
uint32_t lastDeckMs() { return ProtoLink.lastContactMs(); }

void log(const char *fmt, ...) {
  if (!ProtoLink.controllerKnown()) return;
  char b[PL_MAX_PAYLOAD];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  ProtoLink.log("%s", b);
}

}  // namespace pdplink
