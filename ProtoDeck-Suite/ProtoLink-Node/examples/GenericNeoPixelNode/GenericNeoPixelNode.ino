// ============================================================================
//  GenericNeoPixelNode  -  any ESP32 + a WS2812 strip becomes a ProtoDeck node
//  Good for tails, ears, chest panels, or just testing ProtoDeck on your desk.
//
//  Libraries: Adafruit NeoPixel, (optional) Adafruit INA219, ProtoLinkNode
//  Board:     any ESP32 / S2 / S3 / C3 / C6
// ============================================================================
#include <Arduino.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <ProtoLinkNode.h>
#include <pl_effects.h>

// ------------------------------------------------------------------ settings
#define NODE_NAME     "TailNode"   // what shows up in the ProtoDeck list
#define NODE_KIND     "neopixel"
#define LED_PIN       4
#define LED_COUNT     30
#define WIFI_CHANNEL  1            // must match your helmet's soft-AP channel if
                                   // you want ProtoDeck to see both without hopping
#define USE_INA219    0            // 1 = read real current from an INA219 on I2C
#define I2C_SDA       8
#define I2C_SCL       9
#define SUPPLY_VOLTS  5.0f         // used for power estimates without INA219

#if USE_INA219
#include <Wire.h>
#include <Adafruit_INA219.h>
Adafruit_INA219 ina;
bool inaOk = false;
#endif

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
Preferences prefs;

static uint8_t  frame[LED_COUNT * 3];
static PlLedFx  fx = {PL_ZONE_AUX, PL_FX_RAINBOW, 90, 128, 0x22, 0xE4, 0xFF, 0, 0, 0};
static uint32_t identifyUntil = 0;
static float    lastEstimate_mA = 0;

class TailDevice : public ProtoLinkDevice {
 public:
  void fillTelemetry(PlTelemetry &t) override {
#if USE_INA219
    if (inaOk) {
      t.busV = ina.getBusVoltage_V();
      t.currentmA = ina.getCurrent_mA();
      t.powermW = ina.getPower_mW();
    } else
#endif
    {
      t.currentmA = lastEstimate_mA + 60.0f;  // + ESP32 itself
      t.powermW = t.currentmA * SUPPLY_VOLTS;
      t.flags |= PL_TF_ESTIMATED;
    }
    t.tempC = temperatureRead();
    t.brightEars = fx.brightness;
    t.fx = fx.fx;
    snprintf(t.anim, sizeof(t.anim), "%s", plFxName(fx.fx));
  }

  uint8_t onCommand(uint8_t id, const uint8_t *a, uint8_t len, char *msg, size_t ml) override {
    switch (id) {
      case PL_CMD_LED_FX:
        if (len < sizeof(PlLedFx)) return PL_ERR_ARG;
        memcpy(&fx, a, sizeof(PlLedFx));
        if (fx.fx == PL_FX_ANIM) fx.fx = PL_FX_RAINBOW;  // our "native" animation
        snprintf(msg, ml, "fx %s", plFxName(fx.fx));
        return PL_OK;
      case PL_CMD_BRIGHTNESS:
        if (len < 2) return PL_ERR_ARG;
        fx.brightness = a[1];
        snprintf(msg, ml, "brightness %u", a[1]);
        return PL_OK;
      case PL_CMD_IDENTIFY:
        identifyUntil = millis() + 1500;
        snprintf(msg, ml, "blinking");
        return PL_OK;
      case PL_CMD_SAVE:
        prefs.putBytes("fx", &fx, sizeof(fx));
        snprintf(msg, ml, "saved");
        return PL_OK;
      default:
        return PL_ERR_UNSUPPORTED;
    }
  }

  void onText(const char *text, char *reply, size_t rl) override {
    if (!strcmp(text, "help")) {
      snprintf(reply, rl, "commands: help, status, fx <0-%d>, bri <0-255>", PL_FX_COUNT - 1);
    } else if (!strcmp(text, "status")) {
      snprintf(reply, rl, "fx=%s bri=%u leds=%d est=%.0fmA", plFxName(fx.fx), fx.brightness, LED_COUNT,
               lastEstimate_mA);
    } else if (!strncmp(text, "fx ", 3)) {
      int v = atoi(text + 3);
      if (v >= 0 && v < PL_FX_COUNT) fx.fx = (uint8_t)v;
      snprintf(reply, rl, "fx -> %s", plFxName(fx.fx));
    } else if (!strncmp(text, "bri ", 4)) {
      fx.brightness = (uint8_t)constrain(atoi(text + 4), 0, 255);
      snprintf(reply, rl, "brightness -> %u", fx.brightness);
    } else {
      snprintf(reply, rl, "unknown '%s' (try help)", text);
    }
  }
} device;

void setup() {
  Serial.begin(115200);
  strip.begin();
  strip.show();

  prefs.begin("plnode", false);
  if (prefs.getBytesLength("fx") == sizeof(fx)) prefs.getBytes("fx", &fx, sizeof(fx));

#if USE_INA219
  Wire.begin(I2C_SDA, I2C_SCL);
  inaOk = ina.begin();
#endif

  ProtoLinkConfig cfg;
  cfg.name = NODE_NAME;
  cfg.kind = NODE_KIND;
  cfg.caps = PL_CAP_LEDFX | PL_CAP_POWER | PL_CAP_TEXT | PL_CAP_TEMP;
  cfg.ledCount = LED_COUNT;
  cfg.channel = WIFI_CHANNEL;
  ProtoLink.begin(cfg, &device);
}

void loop() {
  ProtoLink.loop();

  static uint32_t lastFrame = 0;
  uint32_t now = millis();
  if (now - lastFrame < 16) return;  // ~60 fps
  lastFrame = now;

  if (identifyUntil && (int32_t)(identifyUntil - now) > 0) {
    bool on = (now / 125) & 1;
    memset(frame, on ? 255 : 0, sizeof(frame));
  } else {
    identifyUntil = 0;
    plfx::render(fx, frame, LED_COUNT, now);
  }

  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, plfx::scale8(frame[i * 3], fx.brightness), plfx::scale8(frame[i * 3 + 1], fx.brightness),
                        plfx::scale8(frame[i * 3 + 2], fx.brightness));
  }
  strip.show();
  lastEstimate_mA = plfx::estimateCurrentmA(frame, LED_COUNT, fx.brightness);
}
