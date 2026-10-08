// ============================================================================
//  protolink.h  -  ProtoLink wire protocol (ESP-NOW)
//  Shared by the ProtoDeck controller (CrowPanel) and every node (helmet,
//  tail, ears, generic NeoPixel boards...). KEEP BOTH COPIES IDENTICAL.
//
//  Every packet = PlHeader (8 bytes) + payload (<= PL_MAX_PAYLOAD bytes).
//  All multi-byte fields are little-endian (native on every ESP32).
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define PL_MAGIC        0xB0      // first byte of every ProtoLink packet
#define PL_VERSION      1
#define PL_MAX_PACKET   250       // ESP-NOW v1 limit
#define PL_MAX_PAYLOAD  (PL_MAX_PACKET - (int)sizeof(PlHeader))

// Change this on BOTH sides if you want your gear to ignore other ProtoDecks
// at a con (it is a filter, not encryption).
#ifndef PL_NET_ID
#define PL_NET_ID       0x0F0F
#endif

// ---------------------------------------------------------------- msg types
enum PlType : uint8_t {
  PL_DISCOVER  = 0x01,  // ctrl -> broadcast            payload: none
  PL_HELLO     = 0x02,  // node -> ctrl                 payload: PlHello
  PL_PING      = 0x03,  // ctrl -> node                 payload: none
  PL_PONG      = 0x04,  // node -> ctrl                 payload: none
  PL_CMD       = 0x10,  // ctrl -> node                 payload: PlCmd + args
  PL_ACK       = 0x11,  // node -> ctrl                 payload: PlAck
  PL_GET       = 0x20,  // ctrl -> node                 payload: 1 byte PlGetWhat
  PL_TELEMETRY = 0x21,  // node -> ctrl                 payload: PlTelemetry
  PL_LIST      = 0x22,  // node -> ctrl                 payload: PlListChunk
  PL_TEXT      = 0x30,  // both ways, free text command / reply (NUL-terminated)
  PL_LOG       = 0x31,  // node -> ctrl, async log line (NUL-terminated)
};

enum PlGetWhat : uint8_t {
  PL_GET_TELEMETRY = 1,
  PL_GET_ANIMS     = 2,
  PL_GET_HELLO     = 3,
};

enum PlListId : uint8_t {
  PL_LIST_ANIMS = 1,
};

// ---------------------------------------------------------------- commands
enum PlCmdId : uint8_t {
  PL_CMD_SET_ANIM    = 1,   // args: NUL-terminated animation name
  PL_CMD_NEXT_ANIM   = 2,
  PL_CMD_PREV_ANIM   = 3,
  PL_CMD_BRIGHTNESS  = 4,   // args: u8 zone, u8 value(0-255)
  PL_CMD_FAN         = 5,   // args: u8 duty(0-255)
  PL_CMD_VISOR_MODE  = 6,   // args: u8 PlVisorMode
  PL_CMD_LED_FX      = 7,   // args: PlLedFx
  PL_CMD_IDENTIFY    = 8,   // flash something so you know which node it is
  PL_CMD_SAVE        = 9,   // persist current settings on the node
  PL_CMD_REBOOT      = 10,
  PL_CMD_CUSTOM      = 0x80 // args: NUL-terminated string, node-specific
};

enum PlZone : uint8_t {
  PL_ZONE_ALL   = 0,
  PL_ZONE_VISOR = 1,
  PL_ZONE_EARS  = 2,
  PL_ZONE_AUX   = 3,   // any extra strip (tail, chest, generic node...)
  PL_ZONE_COUNT
};

enum PlVisorMode : uint8_t {
  PL_VISOR_CUSTOM  = 0,     // colours from the animation file
  PL_VISOR_RAINBOW = 1,
  PL_VISOR_TOGGLE  = 0xFF,
};

// LED effects. PL_FX_ANIM means "stop overriding, let the node do its thing".
enum PlFx : uint8_t {
  PL_FX_ANIM = 0,
  PL_FX_OFF,
  PL_FX_SOLID,
  PL_FX_BREATHE,
  PL_FX_RAINBOW,
  PL_FX_CHASE,
  PL_FX_SCANNER,
  PL_FX_SPARKLE,
  PL_FX_GRADIENT,
  PL_FX_STROBE,
  PL_FX_PLASMA,
  PL_FX_COUNT
};

static inline const char *plFxName(uint8_t fx) {
  static const char *n[PL_FX_COUNT] = {"ANIM", "OFF", "SOLID", "BREATHE", "RAINBOW", "CHASE",
                                       "SCANNER", "SPARKLE", "GRADIENT", "STROBE", "PLASMA"};
  return fx < PL_FX_COUNT ? n[fx] : "?";
}
static inline const char *plZoneName(uint8_t z) {
  static const char *n[PL_ZONE_COUNT] = {"ALL", "VISOR", "EARS", "AUX"};
  return z < PL_ZONE_COUNT ? n[z] : "?";
}

// ---------------------------------------------------------------- caps bits
#define PL_CAP_ANIMS   (1u << 0)  // has named animations
#define PL_CAP_LEDFX   (1u << 1)  // accepts PL_CMD_LED_FX
#define PL_CAP_POWER   (1u << 2)  // reports voltage / current
#define PL_CAP_FAN     (1u << 3)  // has a PWM fan
#define PL_CAP_VISOR   (1u << 4)  // has a visor zone
#define PL_CAP_EARS    (1u << 5)  // has an ears zone
#define PL_CAP_MIC     (1u << 6)  // reports mic level
#define PL_CAP_TEXT    (1u << 7)  // understands PL_TEXT commands
#define PL_CAP_TEMP    (1u << 8)  // reports a temperature

// telemetry flags
#define PL_TF_ESTIMATED (1u << 0) // current/power are estimates, not measured
#define PL_TF_BOOPED    (1u << 1)
#define PL_TF_TILTED    (1u << 2)
#define PL_TF_SPEECH    (1u << 3) // speech detection enabled
#define PL_TF_TALKING   (1u << 4)

// ---------------------------------------------------------------- structs
typedef struct __attribute__((packed)) {
  uint8_t  magic;   // PL_MAGIC
  uint8_t  ver;     // PL_VERSION
  uint8_t  type;    // PlType
  uint8_t  seq;     // request sequence number, echoed in replies
  uint16_t netId;   // PL_NET_ID
  uint8_t  len;     // payload length
  uint8_t  flags;   // reserved
} PlHeader;

typedef struct __attribute__((packed)) {
  char     name[20];   // shown in the node list
  char     kind[12];   // "protogen", "neopixel", "tail"...
  uint8_t  fwMajor, fwMinor;
  uint8_t  channel;    // WiFi channel this node listens on
  uint32_t caps;       // PL_CAP_*
  uint16_t ledCount;
  uint8_t  animCount;
} PlHello;

typedef struct __attribute__((packed)) {
  uint8_t id;          // PlCmdId
  uint8_t args[];      // command-specific
} PlCmd;

typedef struct __attribute__((packed)) {
  uint8_t cmd;         // PlCmdId being acknowledged
  uint8_t status;      // PlStatus
  char    msg[48];     // human readable, NUL-terminated
} PlAck;

enum PlStatus : uint8_t {
  PL_OK = 0, PL_ERR_UNKNOWN = 1, PL_ERR_ARG = 2, PL_ERR_UNSUPPORTED = 3, PL_ERR_BUSY = 4
};

typedef struct __attribute__((packed)) {
  uint8_t zone;        // PlZone
  uint8_t fx;          // PlFx
  uint8_t brightness;  // 0-255 (0xFF..)  applied by the node
  uint8_t speed;       // 0-255, 128 = "normal"
  uint8_t r1, g1, b1;  // primary colour
  uint8_t r2, g2, b2;  // secondary colour (background / gradient end)
} PlLedFx;

typedef struct __attribute__((packed)) {
  uint32_t uptimeMs;
  float    busV;        // volts, NAN if unknown
  float    currentmA;   // milliamps, NAN if unknown
  float    powermW;     // milliwatts, NAN if unknown
  float    tempC;       // NAN if unknown
  uint32_t freeHeap;
  uint8_t  fanDuty;     // 0-255
  uint8_t  brightVisor; // 0-255
  uint8_t  brightEars;  // 0-255
  uint8_t  micLevel;    // 0-100
  uint8_t  flags;       // PL_TF_*
  uint8_t  fx;          // active LED override (PlFx), PL_FX_ANIM when none
  char     anim[24];    // current animation name
} PlTelemetry;

typedef struct __attribute__((packed)) {
  uint8_t listId;      // PlListId
  uint8_t index;       // chunk index
  uint8_t total;       // number of chunks
  char    data[];      // ';'-separated entries, NUL-terminated
} PlListChunk;

#define PL_LIST_CHUNK_DATA (PL_MAX_PAYLOAD - 3)

// ---------------------------------------------------------------- helpers
static inline size_t plBuild(uint8_t *out, uint8_t type, uint8_t seq, const void *payload, size_t len) {
  if (len > (size_t)PL_MAX_PAYLOAD) len = PL_MAX_PAYLOAD;
  PlHeader h;
  h.magic = PL_MAGIC; h.ver = PL_VERSION; h.type = type; h.seq = seq;
  h.netId = PL_NET_ID; h.len = (uint8_t)len; h.flags = 0;
  memcpy(out, &h, sizeof(h));
  if (len && payload) memcpy(out + sizeof(h), payload, len);
  return sizeof(h) + len;
}

// returns payload pointer or nullptr if the packet isn't a valid ProtoLink packet
static inline const uint8_t *plParse(const uint8_t *in, int len, PlHeader *h) {
  if (len < (int)sizeof(PlHeader)) return nullptr;
  memcpy(h, in, sizeof(PlHeader));
  if (h->magic != PL_MAGIC || h->ver != PL_VERSION || h->netId != PL_NET_ID) return nullptr;
  if ((int)sizeof(PlHeader) + h->len > len) return nullptr;
  return in + sizeof(PlHeader);
}
