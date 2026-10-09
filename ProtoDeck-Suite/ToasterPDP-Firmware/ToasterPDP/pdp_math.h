// ============================================================================
//  pdp_math.h  -  pure conversion / battery math (no Arduino, host-testable)
// ============================================================================
#pragma once
#include <math.h>
#include <stdint.h>

namespace pdpmath {

// --------------------------------------------------------------- sensors
// pin millivolts -> volts at VCC
inline float vinFromMv(float mv, float divider, float gain) { return mv * 0.001f * divider * gain; }

// pin millivolts -> amps for an ACS724 (zeroMv and slope as seen at the pin)
inline float ampsFromMv(float mv, float zeroMv, float mvPerA, float frontend, float gain) {
  float slope = mvPerA * frontend * gain;
  return slope > 1.0f ? (mv - zeroMv) / slope : 0.0f;
}

// highest current a sensor can report before its pin reaches limitMv
inline float ampsAtLimit(float limitMv, float zeroMv, float mvPerA, float frontend, float gain) {
  return ampsFromMv(limitMv, zeroMv, mvPerA, frontend, gain);
}

// --------------------------------------------------------------- battery chemistry
struct OcvPoint { float v, soc; };

struct Chem {
  const char *name;
  float emptyV, warnV, critV, fullV, maxV;  // per cell
  const OcvPoint *ocv;
  int nOcv;
  float trustVoltage;                       // 0..1: how much the OCV curve can be trusted mid-range
};

// Resting open-circuit voltage per cell -> state of charge (%). Typical values;
// every cell is a little different, coulomb counting does the fine work.
static const OcvPoint OCV_LIION[] = {
    {3.00f, 0},  {3.30f, 3},  {3.40f, 5},  {3.50f, 8},  {3.60f, 15}, {3.65f, 23}, {3.70f, 33},
    {3.75f, 43}, {3.80f, 52}, {3.85f, 60}, {3.90f, 68}, {4.00f, 80}, {4.10f, 90}, {4.20f, 100}};
static const OcvPoint OCV_LFP[] = {
    {2.50f, 0},  {2.90f, 3},  {3.00f, 6},  {3.10f, 10}, {3.20f, 20}, {3.25f, 30},
    {3.28f, 40}, {3.30f, 60}, {3.32f, 75}, {3.33f, 85}, {3.35f, 92}, {3.40f, 100}};
static const OcvPoint OCV_SLA[] = {  // per 2 V lead-acid cell (12 V block = 6 cells)
    {1.750f, 0},  {1.918f, 10}, {1.943f, 20}, {1.968f, 30}, {1.993f, 40}, {2.017f, 50},
    {2.040f, 60}, {2.062f, 70}, {2.083f, 80}, {2.103f, 90}, {2.122f, 100}};

static const Chem CHEMS[] = {
    {"li-ion", 3.00f, 3.45f, 3.25f, 4.20f, 4.25f, OCV_LIION, sizeof(OCV_LIION) / sizeof(OcvPoint), 1.0f},
    {"lifepo4", 2.50f, 3.10f, 2.90f, 3.40f, 3.70f, OCV_LFP, sizeof(OCV_LFP) / sizeof(OcvPoint), 0.25f},
    {"lead-acid", 1.75f, 1.95f, 1.85f, 2.12f, 2.45f, OCV_SLA, sizeof(OCV_SLA) / sizeof(OcvPoint), 0.8f},
};
constexpr int N_CHEMS = sizeof(CHEMS) / sizeof(CHEMS[0]);

inline const Chem &chem(uint8_t i) { return CHEMS[i < N_CHEMS ? i : 0]; }

inline float socFromCellV(const Chem &c, float v) {
  if (v <= c.ocv[0].v) return 0;
  if (v >= c.ocv[c.nOcv - 1].v) return 100;
  for (int i = 1; i < c.nOcv; i++) {
    if (v <= c.ocv[i].v) {
      const OcvPoint &a = c.ocv[i - 1], &b = c.ocv[i];
      return a.soc + (b.soc - a.soc) * (v - a.v) / (b.v - a.v);
    }
  }
  return 100;
}

// battery-terminal voltage under load -> estimated open-circuit voltage
inline float ocvFromLoaded(float v, float amps, float rintOhm) { return v + amps * rintOhm; }

// --------------------------------------------------------------- fuel gauge
struct Gauge {
  float soc = -1;           // %, -1 = not initialised
  float usedMah = 0, usedWh = 0;
  float avgA = 0;           // slow average of battery current (runtime estimate)
  bool  avgSeeded = false;

  // Initialise from voltage, optionally re-using a stored value if it still agrees.
  void begin(const Chem &c, uint8_t cells, float vin, float amps, float rintOhm, float storedSoc, float tolerance) {
    float vs = socFromCellV(c, ocvFromLoaded(vin, amps, rintOhm) / cells);
    soc = (storedSoc >= 0 && fabsf(storedSoc - vs) <= tolerance) ? storedSoc : vs;
  }

  // dt in seconds. Coulomb counting + a slow pull toward the voltage estimate.
  void update(const Chem &c, uint8_t cells, uint32_t capMah, float vin, float amps, float rintOhm, float dt) {
    if (soc < 0 || dt <= 0) return;
    float mah = amps * dt * (1000.0f / 3600.0f);
    usedMah += mah;
    usedWh += vin * amps * dt / 3600.0f;
    if (capMah > 0) soc -= mah / capMah * 100.0f;

    float cellOcv = ocvFromLoaded(vin, amps, rintOhm) / cells;
    float vs = socFromCellV(c, cellOcv);
    // trust the curve fully near empty/full, and per-chemistry in the flat middle
    float trust = (vs < 15 || vs > 95) ? 1.0f : c.trustVoltage;
    float tau = 1800.0f / (trust > 0.01f ? trust : 0.01f);   // seconds
    soc += (vs - soc) * (dt / tau);
    if (cellOcv <= c.emptyV && soc > 1) soc = 1;               // hard floor at the empty voltage
    if (soc < 0) soc = 0;
    if (soc > 100) soc = 100;

    // ~2 minute average for the runtime estimate
    if (!avgSeeded) { avgA = amps; avgSeeded = true; }
    float k = dt / 120.0f;
    if (k > 1) k = 1;
    avgA += (amps - avgA) * k;
  }

  float remainingMah(uint32_t capMah) const { return soc < 0 ? 0 : capMah * soc / 100.0f; }

  // minutes left at the current average draw; -1 when not meaningful
  int32_t runtimeMin(uint32_t capMah) const {
    if (soc < 0 || avgA < 0.05f) return -1;
    float h = remainingMah(capMah) / (avgA * 1000.0f);
    return h > 99 ? 99 * 60 : (int32_t)(h * 60.0f + 0.5f);
  }
};

// --------------------------------------------------------------- fan curve
inline uint8_t fanAuto(float tempC, float poutW, uint8_t minPct, uint8_t t0, uint8_t t1) {
  float a = t1 > t0 ? (tempC - t0) / (float)(t1 - t0) : 0;
  float b = poutW / 30.0f;   // 30 W on the 5 V rail = full speed (unmodified board reads to ~32 W)
  float x = a > b ? a : b;
  if (x < 0) x = 0;
  if (x > 1) x = 1;
  return (uint8_t)(minPct + (100 - minPct) * x + 0.5f);
}

// --------------------------------------------------------------- hysteresis helper
struct Debounce {
  bool state = false;
  uint32_t since = 0;
  // returns true when the state flips
  bool update(bool want, uint32_t now, uint32_t onMs, uint32_t offMs) {
    if (want == state) { since = now; return false; }
    if (now - since >= (state ? offMs : onMs)) { state = want; since = now; return true; }
    return false;
  }
};

}  // namespace pdpmath
