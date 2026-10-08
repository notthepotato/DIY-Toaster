// ============================================================================
//  radio.h  -  ProtoLink controller side: discovery, peers, requests, retries
//  Everything here runs on the Arduino loop thread; the ESP-NOW receive
//  callback only enqueues raw packets.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <vector>
#include "config.h"
#include "protolink.h"

struct Peer {
  uint8_t     mac[6] = {0};
  PlHello     hello = {};
  int8_t      rssi = 0;
  uint32_t    lastSeen = 0;
  uint32_t    rttMs = 0;
  bool        hasTele = false;
  PlTelemetry tele = {};
  uint32_t    teleAt = 0;
  std::vector<String> anims;
  bool        animsComplete = false;
  uint8_t     animChunksSeen = 0, animChunksTotal = 0;
  String      animsRaw[8];

  bool   online() const { return lastSeen && (millis() - lastSeen) < LINK_OFFLINE_MS; }
  bool   has(uint32_t cap) const { return hello.caps & cap; }
  String macStr() const;
};

enum class RadioEvt : uint8_t {
  PeerFound,   // new node discovered            (peer)
  PeerUpdated, // HELLO from a known node        (peer)
  Pong,        // ping reply                     (peer, value = rtt ms)
  Telemetry,   // fresh telemetry                (peer)
  Anims,       // animation list complete        (peer, value = count)
  Ack,         // command acknowledged           (peer, cmd, status, text)
  Text,        // PL_TEXT reply chunk            (peer, text)
  Log,         // async log line from a node     (peer, text)
  Timeout,     // request got no answer          (peer, cmd = msg type)
  ScanStart,   //                                (value = channel count)
  ScanChannel, // hopping                        (value = channel)
  ScanDone,    //                                (value = nodes online)
};

struct RadioEvent {
  RadioEvt    type;
  int         peer = -1;
  uint8_t     seq = 0;
  uint8_t     cmd = 0;
  uint8_t     status = 0;
  uint32_t    value = 0;
  const char *text = nullptr;
};

typedef void (*RadioListener)(const RadioEvent &e);

namespace radio {

bool begin();
void loop();
void addListener(RadioListener l);

// discovery
bool    startScan(uint8_t onlyChannel = 0);  // 0 = hop channels 1..13
bool    scanning();
uint8_t scanProgress();                      // 0..100

// peers
int   count();
Peer *peer(int i);
int   find(const String &token);             // index, name (prefix, case-insens.), or MAC tail
int   selected();
void  select(int i);
Peer *selectedPeer();

// requests: return the sequence number (>0) or -1 if it couldn't be sent
int ping(int p);
int getTelemetry(int p);
int getAnims(int p);
int command(int p, uint8_t id, const void *args = nullptr, uint8_t len = 0);
int setAnim(int p, const char *name);
int brightness(int p, uint8_t zone, uint8_t value);
int ledFx(int p, const PlLedFx &fx);
int text(int p, const char *s);
bool pending(int p, uint8_t type);           // request of that type in flight?

// radio state
uint8_t  channel();
void     setChannel(uint8_t ch);
uint32_t txCount();
uint32_t rxCount();
uint32_t lostCount();
String   myMac();

const char *statusName(uint8_t st);
const char *cmdName(uint8_t id);

}  // namespace radio
