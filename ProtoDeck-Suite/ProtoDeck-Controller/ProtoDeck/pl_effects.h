// ============================================================================
//  pl_effects.h  -  tiny dependency-free LED effect renderer for ProtoLink
//  Renders a PlLedFx into a plain RGB byte buffer (3 bytes per pixel, R,G,B).
//  FastLED's CRGB has the same memory layout, so you can pass (uint8_t*)leds.
//  Brightness is NOT applied here - the caller scales (showLeds(bri), etc.).
//  Used by nodes to drive strips and by the controller for on-screen preview.
// ============================================================================
#pragma once
#include <stdint.h>
#include "protolink.h"

namespace plfx {

static inline uint8_t qadd8(uint8_t a, uint8_t b) { unsigned s = a + b; return s > 255 ? 255 : s; }
static inline uint8_t scale8(uint8_t v, uint8_t s) { return (uint8_t)(((uint16_t)v * (uint16_t)(s + 1)) >> 8); }
static inline uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t) {
  return (uint8_t)(a + (((int)b - (int)a) * (int)t) / 255);
}
// periodic wave: 0 -> 255 -> 0 over x = 0..255 (smoothstepped triangle, cheap)
static inline uint8_t sin8(uint8_t x) {
  uint32_t tri = x < 128 ? (uint32_t)x * 2 : (uint32_t)(255 - x) * 2;   // 0..254
  uint32_t s = tri * tri * (765 - 2 * tri) / 65025;                       // smoothstep * 255
  return (uint8_t)(s > 255 ? 255 : s);
}
static inline void hsv(uint8_t h, uint8_t s, uint8_t v, uint8_t *o) {
  uint8_t region = h / 43, rem = (h - region * 43) * 6;
  uint8_t p = scale8(v, 255 - s), q = scale8(v, 255 - scale8(s, rem)), t = scale8(v, 255 - scale8(s, 255 - rem));
  switch (region) {
    case 0:  o[0] = v; o[1] = t; o[2] = p; break;
    case 1:  o[0] = q; o[1] = v; o[2] = p; break;
    case 2:  o[0] = p; o[1] = v; o[2] = t; break;
    case 3:  o[0] = p; o[1] = q; o[2] = v; break;
    case 4:  o[0] = t; o[1] = p; o[2] = v; break;
    default: o[0] = v; o[1] = p; o[2] = q; break;
  }
}
static inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
static inline void set(uint8_t *px, uint8_t r, uint8_t g, uint8_t b) { px[0] = r; px[1] = g; px[2] = b; }

// speed 0..255 -> time multiplier (128 = 1x)
static inline uint32_t tscale(uint32_t ms, uint8_t speed) { return (ms * (uint32_t)(speed + 8)) >> 7; }

// Render one frame. `ms` = millis(). Returns false for PL_FX_ANIM (nothing drawn).
static inline bool render(const PlLedFx &f, uint8_t *rgb, uint16_t n, uint32_t ms) {
  if (!n) return false;
  const uint32_t t = tscale(ms, f.speed);
  switch (f.fx) {
    case PL_FX_ANIM:
      return false;

    case PL_FX_OFF:
      for (uint16_t i = 0; i < n; i++) set(rgb + i * 3, 0, 0, 0);
      return true;

    case PL_FX_SOLID:
      for (uint16_t i = 0; i < n; i++) set(rgb + i * 3, f.r1, f.g1, f.b1);
      return true;

    case PL_FX_BREATHE: {
      uint8_t k = sin8((uint8_t)(t >> 4));
      k = scale8(k, k);                                  // gamma-ish, longer "dark" part
      for (uint16_t i = 0; i < n; i++)
        set(rgb + i * 3, lerp8(f.r2, f.r1, k), lerp8(f.g2, f.g1, k), lerp8(f.b2, f.b1, k));
      return true;
    }

    case PL_FX_RAINBOW: {
      uint8_t base = (uint8_t)(t >> 3);
      for (uint16_t i = 0; i < n; i++) hsv((uint8_t)(base + (i * 256u) / n), 255, 255, rgb + i * 3);
      return true;
    }

    case PL_FX_CHASE: {
      // repeating comet: 1 bright head + fading tail every 8 pixels
      uint16_t head = (uint16_t)((t >> 5) % 8);
      for (uint16_t i = 0; i < n; i++) {
        uint8_t d = (uint8_t)((head + 8 - (i % 8)) % 8);   // distance behind the head
        uint8_t k = d == 0 ? 255 : (d < 4 ? (uint8_t)(255 >> (d * 2)) : 0);
        set(rgb + i * 3, lerp8(f.r2, f.r1, k), lerp8(f.g2, f.g1, k), lerp8(f.b2, f.b1, k));
      }
      return true;
    }

    case PL_FX_SCANNER: {
      // larson scanner (KITT eye) bouncing across the strip
      uint32_t span = n > 1 ? (uint32_t)(n - 1) * 2 : 1;
      uint32_t p = (t >> 4) % span;
      int pos = p < (uint32_t)n ? (int)p : (int)(span - p);
      for (uint16_t i = 0; i < n; i++) {
        int d = (int)i - pos; if (d < 0) d = -d;
        uint8_t k = d == 0 ? 255 : (d == 1 ? 120 : (d == 2 ? 40 : (d == 3 ? 10 : 0)));
        set(rgb + i * 3, lerp8(f.r2, f.r1, k), lerp8(f.g2, f.g1, k), lerp8(f.b2, f.b1, k));
      }
      return true;
    }

    case PL_FX_SPARKLE: {
      uint32_t slot = t >> 6;            // new random pattern every "slot"
      uint8_t  ph = (uint8_t)((t & 63) * 4);
      for (uint16_t i = 0; i < n; i++) {
        uint32_t h = hash32(i * 2654435761u ^ slot);
        uint8_t k = 0;
        if ((h & 0x0F) == 0) k = 255 - ph;   // ~1/16 pixels twinkle, fading out
        set(rgb + i * 3, lerp8(f.r2, f.r1, k), lerp8(f.g2, f.g1, k), lerp8(f.b2, f.b1, k));
      }
      return true;
    }

    case PL_FX_GRADIENT: {
      uint8_t shift = (uint8_t)(t >> 4);
      for (uint16_t i = 0; i < n; i++) {
        uint8_t k = sin8((uint8_t)((i * 256u) / n + shift));
        set(rgb + i * 3, lerp8(f.r2, f.r1, k), lerp8(f.g2, f.g1, k), lerp8(f.b2, f.b1, k));
      }
      return true;
    }

    case PL_FX_STROBE: {
      bool on = ((t >> 6) & 3) == 0;
      for (uint16_t i = 0; i < n; i++)
        on ? set(rgb + i * 3, f.r1, f.g1, f.b1) : set(rgb + i * 3, f.r2, f.g2, f.b2);
      return true;
    }

    case PL_FX_PLASMA: {
      for (uint16_t i = 0; i < n; i++) {
        uint8_t a = sin8((uint8_t)(i * 9 + (t >> 3)));
        uint8_t b = sin8((uint8_t)(i * 5 - (t >> 4)));
        uint8_t k = (uint8_t)(((uint16_t)a + b) >> 1);
        set(rgb + i * 3, lerp8(f.r2, f.r1, k), lerp8(f.g2, f.g1, k), lerp8(f.b2, f.b1, k));
      }
      return true;
    }
  }
  return false;
}

// Estimated WS2812 current for a rendered buffer at a given global brightness.
// ~20 mA per colour channel at full drive + ~1 mA idle per pixel.
static inline float estimateCurrentmA(const uint8_t *rgb, uint16_t n, uint8_t brightness) {
  uint32_t sum = 0;
  for (uint32_t i = 0; i < (uint32_t)n * 3; i++) sum += rgb[i];
  return (sum / 255.0f) * 20.0f * (brightness / 255.0f) + n * 1.0f;
}

}  // namespace plfx
