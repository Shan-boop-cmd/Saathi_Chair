// SAATHI chair firmware - signal processing. Port of pipeline.py for the ESP32.
#pragma once
#include <Arduino.h>

struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float z1 = 0, z2 = 0;
  void reset() { z1 = z2 = 0; }
  float step(float x) {                       // transposed direct form II
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

Biquad makeLowpass(float fc, float fs);      // 2nd-order Butterworth
Biquad makeHighpass(float fc, float fs);

// Zero-phase band-pass (high-pass then low-pass, forward and backward), in place.
void bandpassFiltFilt(float* x, int n, float lo, float hi, float fs);
void lowpassFiltFilt(float* x, int n, float fc, float fs);

// Peaks above 40% of the 98th-percentile level, at least minGapS apart. Returns count written to idx.
int detectPeaks(const float* x, int n, float fs, float minGapS, int* idx, int maxPeaks);

// Heart rate from peak times (seconds): median interval -> bpm. quality = share of intervals near the median.
float hrFromPeaks(const float* tS, int n, float* quality);

float medianOf(float* v, int n);             // sorts v in place
float madOf(const float* v, int n, float med);

// Breathing rate (breaths/min): strongest frequency 0.12-0.58 Hz of a beat-by-beat series.
float breathingRate(const float* beatT, const float* value, int n);

struct CheckResult {
  float weightKg = NAN, footZ = NAN, hrBcg = NAN, hrPpg = NAN, hr = NAN;
  float spo2 = NAN, rr = NAN, pttMs = NAN;
  float qBcg = 0, qPpg = 0;
  const char* hrSource = "none";
};

// Full analysis of one 90-second check (same steps as pipeline.process_session).
// bcg: raw ADC (BCG_HZ), red/ir: MAX30102 counts (PPG_HZ). Arrays are modified.
void analyseCheck(int16_t* bcgRaw, int nBcg, float* red, float* ir, int nPpg, CheckResult& out);
