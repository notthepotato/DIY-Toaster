// ============================================================================
//  cli.h  -  one command set for USB serial, the J2 SERIAL header and the
//  ProtoDeck terminal ("say <command>")
// ============================================================================
#pragma once
#include <Arduino.h>

namespace cli {
void begin(Stream &usb, Stream &ext);
void loop();                               // reads both serial ports, runs watch/stream output
void exec(const String &line, Print &out); // run one command, output to `out`
void json(Print &out);                     // one telemetry line as JSON
void status(Print &out);                   // the full dashboard
void broadcast(const char *line);          // print to both serial ports (alarms etc.)
}  // namespace cli

// Collects printed text into a fixed buffer (used for ESP-NOW replies).
class BufPrint : public Print {
 public:
  BufPrint(char *buf, size_t cap) : b(buf), cap(cap) { if (cap) b[0] = 0; }
  size_t write(uint8_t c) override {
    if (n + 1 >= cap) return 0;
    b[n++] = (char)c;
    b[n] = 0;
    return 1;
  }
  size_t length() const { return n; }
 private:
  char *b;
  size_t cap, n = 0;
};
