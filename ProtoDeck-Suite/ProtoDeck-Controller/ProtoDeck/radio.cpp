#include "radio.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_idf_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

String Peer::macStr() const {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(b);
}

namespace radio {
namespace {

const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct RxItem {
  uint8_t mac[6];
  int8_t  rssi;
  uint8_t len;
  uint8_t data[PL_MAX_PACKET];
};

struct Pending {
  bool     used = false;
  uint8_t  seq = 0, type = 0, sub = 0;  // sub = cmd id / get-what
  int      peer = -1;
  uint32_t sentAt = 0;
  bool     retried = false;
  uint8_t  len = 0;
  uint8_t  pkt[PL_MAX_PACKET];
};

QueueHandle_t rxQ = nullptr;
Peer     peers[LINK_MAX_PEERS];
int      nPeers = 0, sel = -1;
Pending  pend[16];
uint8_t  seqCounter = 1, curCh = LINK_HOME_CHANNEL;
uint32_t nTx = 0, nRx = 0, nLost = 0;
RadioListener listeners[4] = {nullptr};

// scan state
bool     scanOn = false;
uint8_t  scanList[14], scanLen = 0, scanIdx = 0;
uint32_t scanStepAt = 0;
bool     scanSecondBurst = false;

void emit(const RadioEvent &e) {
  for (auto l : listeners)
    if (l) l(e);
}

#if ESP_IDF_VERSION_MAJOR >= 5
void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!rxQ || len <= 0 || len > PL_MAX_PACKET || data[0] != PL_MAGIC) return;
  RxItem it;
  memcpy(it.mac, info->src_addr, 6);
  it.rssi = info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : 0;
  it.len = (uint8_t)len;
  memcpy(it.data, data, len);
  xQueueSend(rxQ, &it, 0);
}
#else
void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (!rxQ || len <= 0 || len > PL_MAX_PACKET || data[0] != PL_MAGIC) return;
  RxItem it;
  memcpy(it.mac, mac, 6);
  it.rssi = 0;
  it.len = (uint8_t)len;
  memcpy(it.data, data, len);
  xQueueSend(rxQ, &it, 0);
}
#endif

bool addEspNowPeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.channel = 0;  // "current channel" - we hop globally
  p.ifidx = WIFI_IF_STA;
  p.encrypt = false;
  return esp_now_add_peer(&p) == ESP_OK;
}

int peerByMac(const uint8_t *mac) {
  for (int i = 0; i < nPeers; i++)
    if (!memcmp(peers[i].mac, mac, 6)) return i;
  return -1;
}

uint8_t nextSeq() {
  uint8_t s = seqCounter++;
  if (!seqCounter) seqCounter = 1;
  return s;
}

bool rawSend(const uint8_t *mac, const uint8_t *pkt, size_t n) {
  for (int a = 0; a < 3; a++) {
    esp_err_t e = esp_now_send(mac, pkt, n);
    if (e == ESP_OK) { nTx++; return true; }
    if (e != ESP_ERR_ESPNOW_NO_MEM) return false;
    delay(2);
  }
  return false;
}

// Build + send a request and track it for retry/timeout.
int request(int p, uint8_t type, uint8_t sub, const void *payload, size_t len, bool track = true) {
  if (p < 0 || p >= nPeers || scanOn) return -1;
  Peer &pr = peers[p];
  if (pr.hello.channel && pr.hello.channel != curCh) setChannel(pr.hello.channel);
  if (!addEspNowPeer(pr.mac)) return -1;
  uint8_t seq = nextSeq();
  uint8_t pkt[PL_MAX_PACKET];
  size_t n = plBuild(pkt, type, seq, payload, len);
  if (!rawSend(pr.mac, pkt, n)) return -1;
  if (track) {
    Pending *slot = nullptr;
    for (auto &q : pend)
      if (!q.used) { slot = &q; break; }
    if (!slot) {  // table full: evict the oldest
      slot = &pend[0];
      for (auto &q : pend)
        if (q.sentAt < slot->sentAt) slot = &q;
    }
    slot->used = true;
    slot->seq = seq;
    slot->type = type;
    slot->sub = sub;
    slot->peer = p;
    slot->sentAt = millis();
    slot->retried = false;
    slot->len = (uint8_t)n;
    memcpy(slot->pkt, pkt, n);
  }
  return seq;
}

Pending *resolve(int p, uint8_t seq) {
  for (auto &q : pend)
    if (q.used && q.seq == seq && q.peer == p) return &q;
  return nullptr;
}

void sendDiscover() {
  uint8_t pkt[16];
  size_t n = plBuild(pkt, PL_DISCOVER, 0, nullptr, 0);
  rawSend(BCAST, pkt, n);
}

void finishScan() {
  scanOn = false;
  // go back to the selected node's channel, else wherever most nodes live
  uint8_t best = LINK_HOME_CHANNEL;
  if (sel >= 0 && peers[sel].hello.channel) best = peers[sel].hello.channel;
  else {
    int votes[14] = {0}, top = 0;
    for (int i = 0; i < nPeers; i++)
      if (peers[i].online() && peers[i].hello.channel < 14) votes[peers[i].hello.channel]++;
    for (int c = 1; c < 14; c++)
      if (votes[c] > top) { top = votes[c]; best = c; }
  }
  setChannel(best);
  int on = 0;
  for (int i = 0; i < nPeers; i++)
    if (peers[i].online()) on++;
  RadioEvent e{RadioEvt::ScanDone};
  e.value = on;
  emit(e);
}

void handlePacket(const RxItem &it) {
  PlHeader h;
  const uint8_t *p = plParse(it.data, it.len, &h);
  if (!p) return;
  nRx++;

  int idx = peerByMac(it.mac);
  if (h.type == PL_HELLO && h.len >= sizeof(PlHello)) {
    bool isNew = idx < 0;
    if (isNew) {
      if (nPeers >= LINK_MAX_PEERS) return;
      idx = nPeers++;
      peers[idx] = Peer();
      memcpy(peers[idx].mac, it.mac, 6);
    }
    Peer &pr = peers[idx];
    memcpy(&pr.hello, p, sizeof(PlHello));
    pr.hello.name[sizeof(pr.hello.name) - 1] = 0;
    pr.hello.kind[sizeof(pr.hello.kind) - 1] = 0;
    if (!pr.hello.channel) pr.hello.channel = curCh;
    pr.rssi = it.rssi;
    pr.lastSeen = millis();
    addEspNowPeer(pr.mac);
    if (Pending *q = resolve(idx, h.seq)) q->used = false;
    RadioEvent e{isNew ? RadioEvt::PeerFound : RadioEvt::PeerUpdated};
    e.peer = idx;
    emit(e);
    if (isNew && sel < 0) sel = idx;  // first node found becomes the target
    return;
  }

  if (idx < 0) {  // someone we don't know (e.g. we rebooted): ask who they are
    if (addEspNowPeer(it.mac)) {
      uint8_t pkt[16], what = PL_GET_HELLO;
      size_t n = plBuild(pkt, PL_GET, 0, &what, 1);
      rawSend(it.mac, pkt, n);
    }
    return;
  }

  Peer &pr = peers[idx];
  pr.lastSeen = millis();
  pr.rssi = it.rssi;
  Pending *q = h.seq ? resolve(idx, h.seq) : nullptr;
  if (q) pr.rttMs = millis() - q->sentAt;

  RadioEvent e{RadioEvt::PeerUpdated};
  e.peer = idx;
  e.seq = h.seq;

  switch (h.type) {
    case PL_PONG:
      if (q) q->used = false;
      e.type = RadioEvt::Pong;
      e.value = pr.rttMs;
      emit(e);
      break;

    case PL_TELEMETRY:
      if (q) q->used = false;
      memcpy(&pr.tele, p, h.len < sizeof(PlTelemetry) ? h.len : sizeof(PlTelemetry));
      pr.tele.anim[sizeof(pr.tele.anim) - 1] = 0;
      pr.hasTele = true;
      pr.teleAt = millis();
      e.type = RadioEvt::Telemetry;
      emit(e);
      break;

    case PL_LIST: {
      if (q) q->used = false;
      if (h.len < 4) break;
      const PlListChunk *c = (const PlListChunk *)p;
      if (c->listId != PL_LIST_ANIMS || c->total == 0 || c->total > 8 || c->index >= c->total) break;
      if (c->index == 0 || pr.animChunksTotal != c->total) {
        pr.animChunksSeen = 0;
        pr.animChunksTotal = c->total;
        for (auto &s : pr.animsRaw) s = "";
      }
      String chunk;
      size_t dl = h.len - 3;
      chunk.reserve(dl);
      for (size_t i = 0; i < dl && c->data[i]; i++) chunk += c->data[i];
      pr.animsRaw[c->index] = chunk;
      pr.animChunksSeen |= (1 << c->index);
      if (pr.animChunksSeen == (uint8_t)((1u << c->total) - 1)) {
        String all;
        for (int i = 0; i < c->total; i++) all += pr.animsRaw[i];
        pr.anims.clear();
        int start = 0;
        while (start < (int)all.length()) {
          int semi = all.indexOf(';', start);
          if (semi < 0) semi = all.length();
          String nm = all.substring(start, semi);
          nm.trim();
          if (nm.length()) pr.anims.push_back(nm);
          start = semi + 1;
        }
        pr.animsComplete = true;
        e.type = RadioEvt::Anims;
        e.value = pr.anims.size();
        emit(e);
      }
      break;
    }

    case PL_ACK: {
      if (q) q->used = false;
      static PlAck ack;
      memset(&ack, 0, sizeof(ack));
      memcpy(&ack, p, h.len < sizeof(ack) ? h.len : sizeof(ack));
      ack.msg[sizeof(ack.msg) - 1] = 0;
      e.type = RadioEvt::Ack;
      e.cmd = ack.cmd;
      e.status = ack.status;
      e.text = ack.msg;
      emit(e);
      break;
    }

    case PL_TEXT:
    case PL_LOG: {
      if (q && h.type == PL_TEXT) q->used = false;
      static char txt[PL_MAX_PAYLOAD + 1];
      memcpy(txt, p, h.len);
      txt[h.len] = 0;
      e.type = h.type == PL_TEXT ? RadioEvt::Text : RadioEvt::Log;
      e.text = txt;
      emit(e);
      break;
    }

    default:
      break;
  }
}

void serviceScan() {
  if (!scanOn) return;
  uint32_t now = millis();
  if (!scanSecondBurst && now - scanStepAt >= LINK_SCAN_DWELL_MS / 2) {
    sendDiscover();  // second shout on the same channel catches nodes that were busy
    scanSecondBurst = true;
  }
  if (now - scanStepAt < LINK_SCAN_DWELL_MS) return;
  scanIdx++;
  if (scanIdx >= scanLen) { finishScan(); return; }
  setChannel(scanList[scanIdx]);
  sendDiscover();
  scanStepAt = now;
  scanSecondBurst = false;
  RadioEvent e{RadioEvt::ScanChannel};
  e.value = scanList[scanIdx];
  emit(e);
}

void servicePending() {
  uint32_t now = millis();
  for (auto &q : pend) {
    if (!q.used || now - q.sentAt < LINK_TIMEOUT_MS) continue;
    if (!q.retried && q.peer >= 0 && q.peer < nPeers) {
      Peer &pr = peers[q.peer];
      if (pr.hello.channel && pr.hello.channel != curCh) setChannel(pr.hello.channel);
      rawSend(pr.mac, q.pkt, q.len);
      q.retried = true;
      q.sentAt = now;
      continue;
    }
    q.used = false;
    nLost++;
    RadioEvent e{RadioEvt::Timeout};
    e.peer = q.peer;
    e.seq = q.seq;
    e.cmd = q.type;
    e.status = q.sub;
    emit(e);
  }
}

}  // namespace

bool begin() {
  static bool started = false;
  if (started) return true;
  if (!rxQ) rxQ = xQueueCreate(24, sizeof(RxItem));
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);   // forget any saved AP so the station never locks a channel
  esp_wifi_set_ps(WIFI_PS_NONE);  // lowest latency
  setChannel(LINK_HOME_CHANNEL);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onRecv);
  started = addEspNowPeer(BCAST);
  return started;
}

void loop() {
  RxItem it;
  for (int i = 0; i < 12 && rxQ && xQueueReceive(rxQ, &it, 0) == pdTRUE; i++) handlePacket(it);
  serviceScan();
  servicePending();
}

void addListener(RadioListener l) {
  for (auto &s : listeners)
    if (!s) { s = l; return; }
}

bool startScan(uint8_t onlyChannel) {
  if (scanOn) return false;
  for (auto &q : pend) q.used = false;  // in-flight requests die with the channel hop
  scanLen = 0;
  if (onlyChannel >= 1 && onlyChannel <= 13) scanList[scanLen++] = onlyChannel;
  else
    for (uint8_t c = 1; c <= 13; c++) scanList[scanLen++] = c;
  scanIdx = 0;
  scanOn = true;
  setChannel(scanList[0]);
  sendDiscover();
  scanStepAt = millis();
  scanSecondBurst = false;
  RadioEvent e{RadioEvt::ScanStart};
  e.value = scanLen;
  emit(e);
  RadioEvent c{RadioEvt::ScanChannel};
  c.value = scanList[0];
  emit(c);
  return true;
}
bool scanning() { return scanOn; }
uint8_t scanProgress() { return scanOn ? (uint8_t)((scanIdx * 100) / (scanLen ? scanLen : 1)) : 100; }

int count() { return nPeers; }
Peer *peer(int i) { return (i >= 0 && i < nPeers) ? &peers[i] : nullptr; }
int selected() { return sel; }
void select(int i) {
  if (i >= -1 && i < nPeers) sel = i;
}
Peer *selectedPeer() { return peer(sel); }

int find(const String &tok) {
  if (!tok.length()) return -1;
  bool numeric = true;
  for (char ch : tok)
    if (!isDigit(ch)) numeric = false;
  if (numeric) {
    int i = tok.toInt();
    return (i >= 0 && i < nPeers) ? i : -1;
  }
  String t = tok;
  t.toLowerCase();
  for (int i = 0; i < nPeers; i++) {
    String n = peers[i].hello.name;
    n.toLowerCase();
    if (n.startsWith(t)) return i;
  }
  for (int i = 0; i < nPeers; i++) {  // MAC tail, e.g. "a1:b2" or "a1b2"
    String m = peers[i].macStr();
    m.toLowerCase();
    String m2 = m;
    m2.replace(":", "");
    if (m.endsWith(t) || m2.endsWith(t)) return i;
  }
  return -1;
}

bool pending(int p, uint8_t type) {
  for (auto &q : pend)
    if (q.used && q.peer == p && q.type == type) return true;
  return false;
}

int ping(int p) { return request(p, PL_PING, 0, nullptr, 0); }
int getTelemetry(int p) {
  uint8_t w = PL_GET_TELEMETRY;
  return request(p, PL_GET, w, &w, 1);
}
int getAnims(int p) {
  uint8_t w = PL_GET_ANIMS;
  if (Peer *pr = peer(p)) { pr->animChunksSeen = 0; }
  return request(p, PL_GET, w, &w, 1);
}
int command(int p, uint8_t id, const void *args, uint8_t len) {
  uint8_t buf[PL_MAX_PAYLOAD];
  if (len > PL_MAX_PAYLOAD - 1) len = PL_MAX_PAYLOAD - 1;
  buf[0] = id;
  if (len && args) memcpy(buf + 1, args, len);
  return request(p, PL_CMD, id, buf, 1 + len);
}
int setAnim(int p, const char *name) { return command(p, PL_CMD_SET_ANIM, name, strlen(name) + 1); }
int brightness(int p, uint8_t zone, uint8_t value) {
  uint8_t a[2] = {zone, value};
  return command(p, PL_CMD_BRIGHTNESS, a, 2);
}
int ledFx(int p, const PlLedFx &fx) { return command(p, PL_CMD_LED_FX, &fx, sizeof(fx)); }
int text(int p, const char *s) { return request(p, PL_TEXT, 0, s, strlen(s) + 1); }

uint8_t channel() { return curCh; }
void setChannel(uint8_t ch) {
  if (ch < 1 || ch > 13) return;
  if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) == ESP_OK) curCh = ch;
}
uint32_t txCount() { return nTx; }
uint32_t rxCount() { return nRx; }
uint32_t lostCount() { return nLost; }
String myMac() { return WiFi.macAddress(); }

const char *statusName(uint8_t st) {
  switch (st) {
    case PL_OK: return "OK";
    case PL_ERR_UNKNOWN: return "ERR";
    case PL_ERR_ARG: return "BAD ARG";
    case PL_ERR_UNSUPPORTED: return "UNSUPPORTED";
    case PL_ERR_BUSY: return "BUSY";
  }
  return "?";
}
const char *cmdName(uint8_t id) {
  switch (id) {
    case PL_CMD_SET_ANIM: return "anim";
    case PL_CMD_NEXT_ANIM: return "next";
    case PL_CMD_PREV_ANIM: return "prev";
    case PL_CMD_BRIGHTNESS: return "brightness";
    case PL_CMD_FAN: return "fan";
    case PL_CMD_VISOR_MODE: return "visor";
    case PL_CMD_LED_FX: return "fx";
    case PL_CMD_IDENTIFY: return "identify";
    case PL_CMD_SAVE: return "save";
    case PL_CMD_REBOOT: return "reboot";
    case PL_CMD_CUSTOM: return "custom";
  }
  return "cmd";
}

}  // namespace radio
