// ============================================================================
//  ProtoLinkNode  -  make any ESP32 discoverable & controllable by ProtoDeck
//
//  1. Implement a ProtoLinkDevice (telemetry + command handlers)
//  2. ProtoLink.begin(cfg, &device)   (after WiFi.softAP()/WiFi.begin() if you use WiFi)
//  3. ProtoLink.loop()                (every loop() iteration)
//
//  Radio callbacks only enqueue packets; all your handlers run inside loop(),
//  so it is safe to touch your globals / LEDs / config from them.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "protolink.h"

class ProtoLinkDevice {
 public:
  virtual ~ProtoLinkDevice() {}
  // Fill in whatever you know. Unknown floats should stay NAN (pre-filled).
  virtual void fillTelemetry(PlTelemetry &t) = 0;
  // Animation list (names without file extension). Return 0 if not supported.
  virtual uint16_t animCount() { return 0; }
  virtual const char *animName(uint16_t i) { (void)i; return nullptr; }
  // Handle a PL_CMD_*; return a PlStatus and optionally write a short message.
  virtual uint8_t onCommand(uint8_t id, const uint8_t *args, uint8_t len, char *msg, size_t msgLen) = 0;
  // Free-text command from the ProtoDeck terminal ("say ..."). Reply may be long,
  // it is split into several packets automatically.
  virtual void onText(const char *text, char *reply, size_t replyLen) {
    (void)text; snprintf(reply, replyLen, "no text commands on this node");
  }
};

struct ProtoLinkConfig {
  const char *name     = "ProtoNode";  // up to 19 chars shown on the ProtoDeck
  const char *kind     = "generic";    // up to 11 chars ("protogen", "tail", ...)
  uint32_t    caps     = 0;            // PL_CAP_* bitmask
  uint16_t    ledCount = 0;
  uint8_t     channel  = 1;            // only used if WiFi isn't running yet
  uint8_t     fwMajor  = 1, fwMinor = 0;
};

class ProtoLinkNode {
 public:
  bool begin(const ProtoLinkConfig &cfg, ProtoLinkDevice *dev);
  void loop();
  // Push an async log line to the last controller that talked to us.
  bool log(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  bool     controllerKnown() const { return _haveCtrl; }
  uint32_t lastContactMs() const { return _lastContact; }
  uint8_t  channel() const { return _channel; }
  bool     started() const { return _started; }

 private:
  void handle(const uint8_t *mac, const uint8_t *data, int len);
  bool send(const uint8_t *mac, uint8_t type, uint8_t seq, const void *payload, size_t len);
  bool ensurePeer(const uint8_t *mac);
  void sendHello(const uint8_t *mac, uint8_t seq);
  void sendAnims(const uint8_t *mac, uint8_t seq);
  void sendLongText(const uint8_t *mac, uint8_t type, uint8_t seq, const char *text);

  ProtoLinkConfig  _cfg;
  ProtoLinkDevice *_dev = nullptr;
  bool     _started = false, _haveCtrl = false;
  uint8_t  _ctrl[6] = {0};
  uint8_t  _channel = 1;
  int      _ifidx = 0;
  uint32_t _lastContact = 0;
  uint32_t _rebootAt = 0;
};

extern ProtoLinkNode ProtoLink;
