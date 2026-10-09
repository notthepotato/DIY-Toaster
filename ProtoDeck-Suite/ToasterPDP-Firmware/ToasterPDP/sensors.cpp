#include "sensors.h"
#include "config.h"
#include "pdp_math.h"
#include "settings.h"

namespace sensors {
namespace {

Readings r;
uint32_t lastBlock = 0, lastTemp = 0, inLimitAt = 0, outLimitAt = 0;
bool seeded = false;
constexpr float ALPHA = (float)SAMPLE_PERIOD_MS / 250.0f;  // ~250 ms EMA

struct Block { float vin, in, out, inMax, outMax; };

Block readBlock() {
  Block b = {0, 0, 0, 0, 0};
  for (int i = 0; i < SAMPLES_PER_BLOCK; i++) {
    float v = analogReadMilliVolts(PIN_VIN_DIV);
    float a = analogReadMilliVolts(PIN_ISENSE_IN);
    float o = analogReadMilliVolts(PIN_ISENSE_OUT);
    b.vin += v;
    b.in += a;
    b.out += o;
    if (a > b.inMax) b.inMax = a;
    if (o > b.outMax) b.outMax = o;
  }
  b.vin /= SAMPLES_PER_BLOCK;
  b.in /= SAMPLES_PER_BLOCK;
  b.out /= SAMPLES_PER_BLOCK;
  return b;
}

}  // namespace

const Readings &get() { return r; }

void begin() {
  analogReadResolution(12);
  for (int p : {PIN_VIN_DIV, PIN_ISENSE_IN, PIN_ISENSE_OUT}) {
    pinMode(p, INPUT);
    analogSetPinAttenuation(p, ADC_11db);  // widest range (called 12 dB in ESP-IDF)
  }
  // seed the filters so the first readings aren't a slow ramp from zero
  Block b = readBlock();
  r.vinMv = b.vin;
  r.inMv = b.in;
  r.outMv = b.out;
  seeded = true;
  update();
  resetStats();
}

void resetStats() {
  r.iinPeak = r.iin > 0 ? r.iin : 0;
  r.ioutPeak = r.iout > 0 ? r.iout : 0;
  r.vinMin = r.vinMax = r.vin;
}

bool update() {
  uint32_t now = millis();
  if (seeded && now - lastBlock < SAMPLE_PERIOD_MS) return false;
  lastBlock = now;
  const Settings &s = settings::get();
  Block b = readBlock();

  r.vinMv += (b.vin - r.vinMv) * ALPHA;
  r.inMv += (b.in - r.inMv) * ALPHA;
  r.outMv += (b.out - r.outMv) * ALPHA;

  r.vin = pdpmath::vinFromMv(r.vinMv, VIN_DIVIDER, s.vinGain);
  r.iin = pdpmath::ampsFromMv(r.inMv, s.inZeroMv, ACS_MV_PER_A, s.inFrontend, s.inGain);
  r.iout = pdpmath::ampsFromMv(r.outMv, s.outZeroMv, ACS_MV_PER_A, s.outFrontend, s.outGain);
  r.batteryPresent = r.vin > 3.0f;
  if (!r.batteryPresent) r.iin = 0;  // USB-only: the input sensor just reads noise
  r.pin = r.vin * r.iin;
  r.pout = VOUT_NOMINAL * r.iout;

  // statistics from the fast (unfiltered) block values
  float vinFast = pdpmath::vinFromMv(b.vin, VIN_DIVIDER, s.vinGain);
  float inFast = pdpmath::ampsFromMv(b.in, s.inZeroMv, ACS_MV_PER_A, s.inFrontend, s.inGain);
  float outFast = pdpmath::ampsFromMv(b.out, s.outZeroMv, ACS_MV_PER_A, s.outFrontend, s.outGain);
  if (r.batteryPresent && inFast > r.iinPeak) r.iinPeak = inFast;
  if (outFast > r.ioutPeak) r.ioutPeak = outFast;
  if (r.batteryPresent) {
    if (r.vinMin < 3.0f || vinFast < r.vinMin) r.vinMin = vinFast;
    if (vinFast > r.vinMax) r.vinMax = vinFast;
  }

  if (b.inMax >= ADC_LIMIT_MV) inLimitAt = now;
  if (b.outMax >= ADC_LIMIT_MV) outLimitAt = now;
  r.inNearLimit = inLimitAt && now - inLimitAt < 1000;
  r.outNearLimit = outLimitAt && now - outLimitAt < 1000;
  r.inMaxReadableA = pdpmath::ampsAtLimit(ADC_LIMIT_MV, s.inZeroMv, ACS_MV_PER_A, s.inFrontend, s.inGain);
  r.outMaxReadableA = pdpmath::ampsAtLimit(ADC_LIMIT_MV, s.outZeroMv, ACS_MV_PER_A, s.outFrontend, s.outGain);

  if (now - lastTemp > 1000 || isnan(r.tempC)) {
    lastTemp = now;
    float t = temperatureRead();
    r.tempC = isnan(r.tempC) ? t : r.tempC + (t - r.tempC) * 0.3f;
  }
  r.blocks++;
  return true;
}

void averagePins(uint32_t ms, float &vinMv, float &inMv, float &outMv) {
  double sv = 0, si = 0, so = 0;
  uint32_t n = 0, start = millis();
  while (millis() - start < ms) {
    Block b = readBlock();
    sv += b.vin;
    si += b.in;
    so += b.out;
    n++;
    delay(2);
  }
  vinMv = n ? sv / n : 0;
  inMv = n ? si / n : 0;
  outMv = n ? so / n : 0;
}

}  // namespace sensors
