#include "ProtoLinkNode.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_idf_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <math.h>
#include <stdarg.h>

ProtoLinkNode ProtoLink;

namespace {
struct RxItem {
  uint8_t mac[6];
  uint8_t len;
  uint8_t data[PL_MAX_PACKET];
};
QueueHandle_t rxQ = nullptr;

void pushRx(const uint8_t *mac, const uint8_t *data, int len) {
  if (!rxQ || len <= 0 || len > PL_MAX_PACKET || data[0] != PL_MAGIC) return;
  RxItem it;
  memcpy(it.mac, mac, 6);
  it.len = (uint8_t)len;
  memcpy(it.data, data, len);
  xQueueSend(rxQ, &it, 0);  // runs in the WiFi task: never block here
}

#if ESP_IDF_VERSION_MAJOR >= 5
void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) { pushRx(info->src_addr, data, len); }
#else
void onRecv(const uint8_t *mac, const uint8_t *data, int len) { pushRx(mac, data, len); }
#endif

void copyStr(char *dst, size_t cap, const char *src) {
  if (!cap) return;
  strncpy(dst, src ? src : "", cap - 1);
  dst[cap - 1] = 0;
}
}  // namespace

bool ProtoLinkNode::begin(const ProtoLinkConfig &cfg, ProtoLinkDevice *dev) {
  _cfg = cfg;
  _dev = dev;
  if (!rxQ) rxQ = xQueueCreate(12, sizeof(RxItem));

  wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_MODE_NULL) {  // nobody started WiFi: run as a bare station on cfg.channel
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(cfg.channel, WIFI_SECOND_CHAN_NONE);
    mode = WIFI_MODE_STA;
  }
  // Send from the interface that is actually up. Helmet firmware runs a soft-AP.
  _ifidx = (mode == WIFI_MODE_AP) ? WIFI_IF_AP : WIFI_IF_STA;

  uint8_t ch = 1;
  wifi_second_chan_t sec;
  if (esp_wifi_get_channel(&ch, &sec) == ESP_OK) _channel = ch;

  esp_err_t err = esp_now_init();
  if (err != ESP_OK) {
    Serial.printf("[ProtoLink] esp_now_init failed: %d\n", (int)err);
    return false;
  }
  esp_now_register_recv_cb(onRecv);
  _started = true;
  Serial.printf("[ProtoLink] node '%s' up on channel %u (%s), MAC %s\n", _cfg.name, _channel,
                _ifidx == WIFI_IF_AP ? "AP" : "STA",
                _ifidx == WIFI_IF_AP ? WiFi.softAPmacAddress().c_str() : WiFi.macAddress().c_str());
  return true;
}

bool ProtoLinkNode::ensurePeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.channel = 0;  // follow the current channel
  p.ifidx = (wifi_interface_t)_ifidx;
  p.encrypt = false;
  esp_err_t e = esp_now_add_peer(&p);
  if (e == ESP_ERR_ESPNOW_FULL) {  // forget an old controller and retry
    esp_now_peer_info_t old;
    if (esp_now_fetch_peer(true, &old) == ESP_OK) esp_now_del_peer(old.peer_addr);
    e = esp_now_add_peer(&p);
  }
  return e == ESP_OK;
}

bool ProtoLinkNode::send(const uint8_t *mac, uint8_t type, uint8_t seq, const void *payload, size_t len) {
  if (!_started || !ensurePeer(mac)) return false;
  uint8_t buf[PL_MAX_PACKET];
  size_t n = plBuild(buf, type, seq, payload, len);
  for (int attempt = 0; attempt < 3; attempt++) {
    esp_err_t e = esp_now_send(mac, buf, n);
    if (e == ESP_OK) return true;
    if (e != ESP_ERR_ESPNOW_NO_MEM) return false;
    delay(3);  // TX queue full: give the radio a moment
  }
  return false;
}

void ProtoLinkNode::sendHello(const uint8_t *mac, uint8_t seq) {
  PlHello h = {};
  copyStr(h.name, sizeof(h.name), _cfg.name);
  copyStr(h.kind, sizeof(h.kind), _cfg.kind);
  h.fwMajor = _cfg.fwMajor;
  h.fwMinor = _cfg.fwMinor;
  h.channel = _channel;
  h.caps = _cfg.caps;
  h.ledCount = _cfg.ledCount;
  uint16_t ac = _dev ? _dev->animCount() : 0;
  h.animCount = ac > 255 ? 255 : (uint8_t)ac;
  send(mac, PL_HELLO, seq, &h, sizeof(h));
}

void ProtoLinkNode::sendAnims(const uint8_t *mac, uint8_t seq) {
  // Build the full list once, then split into chunks.
  static char all[8 * (PL_LIST_CHUNK_DATA - 1) + 1];  // controller accepts up to 8 chunks
  size_t pos = 0;
  uint16_t n = _dev ? _dev->animCount() : 0;
  for (uint16_t i = 0; i < n && pos < sizeof(all) - 2; i++) {
    const char *nm = _dev->animName(i);
    if (!nm) continue;
    int w = snprintf(all + pos, sizeof(all) - pos, "%s;", nm);
    if (w < 0) break;
    pos += (size_t)w;
    if (pos >= sizeof(all)) { pos = sizeof(all) - 1; break; }
  }
  all[pos] = 0;
  const size_t per = PL_LIST_CHUNK_DATA - 1;
  uint8_t total = (uint8_t)(pos / per + 1);
  uint8_t buf[PL_MAX_PAYLOAD];
  for (uint8_t i = 0; i < total; i++) {
    PlListChunk *c = (PlListChunk *)buf;
    c->listId = PL_LIST_ANIMS;
    c->index = i;
    c->total = total;
    size_t off = (size_t)i * per, cnt = pos > off ? pos - off : 0;
    if (cnt > per) cnt = per;
    memcpy(c->data, all + off, cnt);
    c->data[cnt] = 0;
    send(mac, PL_LIST, seq, buf, 3 + cnt + 1);
    delay(2);
  }
}

void ProtoLinkNode::sendLongText(const uint8_t *mac, uint8_t type, uint8_t seq, const char *text) {
  size_t len = strlen(text);
  const size_t per = PL_MAX_PAYLOAD - 1;
  char buf[PL_MAX_PAYLOAD];
  size_t off = 0;
  do {
    size_t cnt = len - off > per ? per : len - off;
    memcpy(buf, text + off, cnt);
    buf[cnt] = 0;
    send(mac, type, seq, buf, cnt + 1);
    off += cnt;
    if (off < len) delay(2);
  } while (off < len);
}

bool ProtoLinkNode::log(const char *fmt, ...) {
  if (!_haveCtrl) return false;
  char line[PL_MAX_PAYLOAD];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  return send(_ctrl, PL_LOG, 0, line, strlen(line) + 1);
}

void ProtoLinkNode::handle(const uint8_t *mac, const uint8_t *data, int len) {
  PlHeader h;
  const uint8_t *p = plParse(data, len, &h);
  if (!p) return;
  memcpy(_ctrl, mac, 6);
  _haveCtrl = true;
  _lastContact = millis();

  switch (h.type) {
    case PL_DISCOVER:
      delay(esp_random() % 15);  // spread replies when many nodes answer at once
      sendHello(mac, h.seq);
      break;

    case PL_PING:
      send(mac, PL_PONG, h.seq, nullptr, 0);
      break;

    case PL_GET: {
      uint8_t what = h.len ? p[0] : (uint8_t)PL_GET_TELEMETRY;
      if (what == PL_GET_TELEMETRY) {
        PlTelemetry t = {};
        t.busV = t.currentmA = t.powermW = t.tempC = NAN;
        t.fx = PL_FX_ANIM;
        t.uptimeMs = millis();
        t.freeHeap = ESP.getFreeHeap();
        if (_dev) _dev->fillTelemetry(t);
        send(mac, PL_TELEMETRY, h.seq, &t, sizeof(t));
      } else if (what == PL_GET_ANIMS) {
        sendAnims(mac, h.seq);
      } else if (what == PL_GET_HELLO) {
        sendHello(mac, h.seq);
      }
      break;
    }

    case PL_CMD: {
      if (!h.len) break;
      PlAck ack = {};
      ack.cmd = p[0];
      if (p[0] == PL_CMD_REBOOT) {
        ack.status = PL_OK;
        copyStr(ack.msg, sizeof(ack.msg), "rebooting");
        _rebootAt = millis() + 250;
      } else if (_dev) {
        ack.status = _dev->onCommand(p[0], p + 1, (uint8_t)(h.len - 1), ack.msg, sizeof(ack.msg));
      } else {
        ack.status = PL_ERR_UNSUPPORTED;
      }
      ack.msg[sizeof(ack.msg) - 1] = 0;
      send(mac, PL_ACK, h.seq, &ack, sizeof(ack));
      break;
    }

    case PL_TEXT: {
      static char in[PL_MAX_PAYLOAD + 1];
      static char out[1024];
      size_t n = h.len;
      memcpy(in, p, n);
      in[n] = 0;
      out[0] = 0;
      if (_dev) _dev->onText(in, out, sizeof(out));
      if (!out[0]) copyStr(out, sizeof(out), "ok");
      sendLongText(mac, PL_TEXT, h.seq, out);
      break;
    }
    default:
      break;
  }
}

void ProtoLinkNode::loop() {
  if (!_started || !rxQ) return;
  RxItem it;
  for (int i = 0; i < 6 && xQueueReceive(rxQ, &it, 0) == pdTRUE; i++) handle(it.mac, it.data, it.len);
  if (_rebootAt && (int32_t)(millis() - _rebootAt) >= 0) ESP.restart();
}
