// SAATHI chair firmware - sensor drivers (real chair) and a simulated set for SAATHI_DEMO builds.
#include "sensors.h"
#include "config.h"
#include "storage.h"
#include <Wire.h>
#include <HX711.h>
#include <MAX30105.h>

static HX711 scale;
static MAX30105 ppg;
static bool ppgOk = false;

// ---------------- buttons ----------------
struct Button {
  int pin; bool last = true; uint32_t t = 0;
  bool pressed() {
    bool now = digitalRead(pin);
    if (now != last && millis() - t > 40) { t = millis(); last = now; if (!now) return true; }
    return false;
  }
};
static Button bGreen{PIN_BTN_GREEN}, bRed{PIN_BTN_RED}, bSos{PIN_BTN_SOS};
bool greenPressed() { return bGreen.pressed(); }
bool redPressed() { return bRed.pressed(); }
bool sosPressed() { return bSos.pressed(); }
void ledSet(bool on) { digitalWrite(PIN_LED, on); }

bool chargerPlugged() {
#if SAATHI_DEMO
  return false;
#else
  return digitalRead(PIN_CHARGER) == HIGH;
#endif
}

float batteryVolts() { return 2.0f * analogReadMilliVolts(PIN_BATT_ADC) / 1000.0f; }

bool sensorsBegin() {
  pinMode(PIN_BTN_GREEN, INPUT_PULLUP); pinMode(PIN_BTN_RED, INPUT_PULLUP); pinMode(PIN_BTN_SOS, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT); pinMode(PIN_CHARGER, INPUT);
  delay(5);
  // start from each button's real state, so a stuck or shorted button never fires a press (or a false SOS) at boot
  bGreen.last = digitalRead(PIN_BTN_GREEN); bRed.last = digitalRead(PIN_BTN_RED); bSos.last = digitalRead(PIN_BTN_SOS);
  pinMode(PIN_Z_MODE_BODY, OUTPUT); pinMode(PIN_Z_MODE_CURR, OUTPUT);
  digitalWrite(PIN_Z_MODE_BODY, LOW); digitalWrite(PIN_Z_MODE_CURR, LOW);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
#if SAATHI_DEMO
  return true;
#else
  scale.begin(PIN_HX_DOUT, PIN_HX_SCK);
  scale.set_scale(settings().hxScale);
  scale.set_offset(settings().hxOffset);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  ppgOk = ppg.begin(Wire, I2C_SPEED_FAST);
  if (ppgOk) {
    // brightness 0x1F, average 1, mode 2 = red + IR, 100 samples/s, pulse width 411 us, ADC range 16384
    ppg.setup(0x1F, 1, 2, PPG_HZ, 411, 16384);
  }
  return ppgOk;
#endif
}

// ---------------- weight ----------------
float readWeightKg() {
#if SAATHI_DEMO
  return demoState().sitting ? demoState().trueWeight : 0.0f;
#else
  if (!scale.is_ready()) return NAN;
  return scale.get_units(1);
#endif
}

void tareChair() {
#if !SAATHI_DEMO
  scale.tare(20);
  settings().hxOffset = scale.get_offset();
  saveSettings();
#endif
}

// Re-zero the empty chair (slow drift of the load cells: creep, temperature). Called only after 10 minutes empty
// with a reading already within 0.3 kg of zero, so a bag or cushion left on the seat is never zeroed away.
void autoZero() {
#if !SAATHI_DEMO
  scale.tare(10);
  long off = scale.get_offset();
  if (labs(off - settings().hxOffset) > 50) { settings().hxOffset = off; saveSettings(); }
#endif
}

void calibrateWeight(float knownKg) {
#if !SAATHI_DEMO
  long raw = scale.read_average(20) - scale.get_offset();
  if (knownKg > 0 && raw != 0) {
    settings().hxScale = raw / knownKg;
    scale.set_scale(settings().hxScale);
    saveSettings();
  }
#endif
}

// ---------------- foot impedance ----------------
static float zReadDc() {
  long acc = 0;
  for (int i = 0; i < Z_ADC_SAMPLES; i++) { acc += analogReadMilliVolts(PIN_Z_ADC); delayMicroseconds(150); }
  return acc / (float)Z_ADC_SAMPLES;
}

float measureFootImpedance(bool implant) {
  if (implant || chargerPlugged()) return NAN;             // safety interlock
#if SAATHI_DEMO
  return demoState().trueZ * (1 + 0.004f * (random(-100, 100) / 100.0f));
#else
  digitalWrite(PIN_Z_MODE_BODY, LOW); digitalWrite(PIN_Z_MODE_CURR, LOW);
  float v0 = zReadDc();                                    // rectifier offset with no signal selected
  ledcAttach(PIN_Z_DRIVE, Z_DRIVE_HZ, 6);                  // 50 kHz, 6-bit resolution (ESP32 core 3.x API)
  ledcWrite(PIN_Z_DRIVE, 32);                              // 50% duty
  digitalWrite(PIN_Z_MODE_BODY, HIGH); delay(600);         // let the 100k/1uF filter settle
  float vBody = zReadDc() - v0;
  digitalWrite(PIN_Z_MODE_BODY, LOW); digitalWrite(PIN_Z_MODE_CURR, HIGH); delay(600);
  float vCurr = zReadDc() - v0;
  digitalWrite(PIN_Z_MODE_CURR, LOW);
  ledcWrite(PIN_Z_DRIVE, 0); ledcDetach(PIN_Z_DRIVE);
  if (vCurr < 20 || vBody < 5) return NAN;                  // feet not on the plates / no current
  float z = Z_RCS_OHM * vBody / vCurr * settings().zCal;
  return (z > 150 && z < 1500) ? z : NAN;
#endif
}

// ---------------- the 90-second check ----------------
void measureCheck(CheckResult& out, bool (*keepGoing)()) {
  out = CheckResult();
#if SAATHI_DEMO
  DemoState& s = demoState();
  uint32_t t0 = millis();
  while (millis() - t0 < 3000) { if (!keepGoing()) return; delay(20); }   // short wait in the demo
  out.weightKg = s.trueWeight + random(-30, 30) / 100.0f;
  out.hr = s.trueHr + random(-20, 20) / 10.0f; out.hrBcg = out.hr; out.hrPpg = out.hr; out.hrSource = "both";
  out.spo2 = s.trueSpo2 + random(-8, 8) / 10.0f;
  out.rr = s.trueRr + random(-20, 20) / 10.0f;
  out.pttMs = 256 + random(-8, 8);
  return;
#else
  const int nB = CHECK_SECONDS * BCG_HZ, nP = CHECK_SECONDS * PPG_HZ;
  int16_t* bcg = (int16_t*)malloc(nB * sizeof(int16_t));
  float* red = (float*)malloc(nP * sizeof(float));
  float* ir = (float*)malloc(nP * sizeof(float));
  float* w = (float*)malloc(CHECK_SECONDS * 12 * sizeof(float));   // HX711 at 10-80 SPS; stored up to 12/s
  if (!bcg || !red || !ir || !w) { free(bcg); free(red); free(ir); free(w); return; }
  int iB = 0, iP = 0, iW = 0, half = 0; int32_t acc = 0;
  const uint32_t period = 1000000UL / (BCG_HZ * 2);                // ADC at 500 Hz, pairs averaged
  uint32_t next = micros(), tEnd = millis() + CHECK_SECONDS * 1000UL, lastW = 0;
  if (ppgOk) ppg.clearFIFO();
  while ((int32_t)(millis() - tEnd) < 0) {
    if ((int32_t)(micros() - next) >= 0) {
      next += period;
      acc += analogRead(PIN_BCG_ADC);
      if (++half == 2) { if (iB < nB) bcg[iB++] = (int16_t)(acc / 2); acc = 0; half = 0; }
    }
    if (ppgOk) {
      ppg.check();
      while (ppg.available() && iP < nP) { red[iP] = ppg.getFIFORed(); ir[iP] = ppg.getFIFOIR(); iP++; ppg.nextSample(); }
    }
    if (millis() - lastW >= 90 && scale.is_ready() && iW < (int)(CHECK_SECONDS * 12)) { w[iW++] = scale.get_units(1); lastW = millis(); }
    if (!keepGoing()) break;
  }
  // weight (same rule as pipeline.process_weight): the highest steady 10-s plateau, accepted only if the person sat at
  // that load for half the measurement and the value is in range (a broken bridge wire drives the HX711 to the rail)
  int perSec = iW / CHECK_SECONDS; if (perSec < 1) perSec = 1;
  int win = 10 * perSec, start = SETTLE_SECONDS * perSec;
  float full = NAN;
  for (int i = start; i + win <= iW; i += perSec) {
    float m = 0, v = 0;
    for (int k = 0; k < win; k++) m += w[i + k];
    m /= win;
    for (int k = 0; k < win; k++) v += (w[i + k] - m) * (w[i + k] - m);
    if (sqrtf(v / win) <= 0.5f) {
      float tmp[1200]; int nn = win < 1200 ? win : 1200;
      memcpy(tmp, w + i, nn * sizeof(float));
      float md = medianOf(tmp, nn);
      if (isnan(full) || md > full) full = md;
    }
  }
  if (!isnan(full)) {
    int near = 0;
    for (int i = start; i < iW; i++) if (fabsf(w[i] - full) < 1.0f) near++;
    bool steady = iW > start && near >= FULL_LOAD_FRAC * (iW - start);
    if (steady && full >= WEIGHT_MIN_KG && full <= WEIGHT_MAX_KG) out.weightKg = full;   // load cells tared on the empty chair
  }
  analyseCheck(bcg, iB, red, ir, ppgOk ? iP : 0, out);
  free(bcg); free(red); free(ir); free(w);
#endif
}
