// SAATHI chair firmware - signal processing (port of pipeline.py).
#include "dsp.h"
#include "config.h"
#include <math.h>
#include <stdlib.h>

static int cmpFloat(const void* a, const void* b) {
  float x = *(const float*)a, y = *(const float*)b;
  return (x > y) - (x < y);
}

Biquad makeLowpass(float fc, float fs) {        // RBJ cookbook, Q = 1/sqrt(2)
  Biquad q; float w = 2 * PI * fc / fs, c = cosf(w), s = sinf(w), al = s / (2 * 0.7071f), a0 = 1 + al;
  q.b0 = (1 - c) / 2 / a0; q.b1 = (1 - c) / a0; q.b2 = q.b0; q.a1 = -2 * c / a0; q.a2 = (1 - al) / a0;
  return q;
}
Biquad makeHighpass(float fc, float fs) {
  Biquad q; float w = 2 * PI * fc / fs, c = cosf(w), s = sinf(w), al = s / (2 * 0.7071f), a0 = 1 + al;
  q.b0 = (1 + c) / 2 / a0; q.b1 = -(1 + c) / a0; q.b2 = q.b0; q.a1 = -2 * c / a0; q.a2 = (1 - al) / a0;
  return q;
}

static void runBiquad(Biquad f, float* x, int n, bool backward) {
  f.reset();
  // start from the first sample's steady state to avoid a big edge transient
  float x0 = backward ? x[n - 1] : x[0];
  for (int k = 0; k < 200; k++) f.step(x0);
  if (!backward) for (int i = 0; i < n; i++) x[i] = f.step(x[i]);
  else for (int i = n - 1; i >= 0; i--) x[i] = f.step(x[i]);
}

void bandpassFiltFilt(float* x, int n, float lo, float hi, float fs) {
  Biquad h = makeHighpass(lo, fs), l = makeLowpass(hi, fs);
  runBiquad(h, x, n, false); runBiquad(l, x, n, false);
  runBiquad(h, x, n, true);  runBiquad(l, x, n, true);
}

void lowpassFiltFilt(float* x, int n, float fc, float fs) {
  Biquad l = makeLowpass(fc, fs);
  runBiquad(l, x, n, false); runBiquad(l, x, n, true);
}

float medianOf(float* v, int n) {
  if (n <= 0) return NAN;
  qsort(v, n, sizeof(float), cmpFloat);
  return (n % 2) ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
}

float madOf(const float* v, int n, float med) {
  if (n <= 0) return NAN;
  float* d = (float*)malloc(n * sizeof(float));
  if (!d) return NAN;
  for (int i = 0; i < n; i++) d[i] = fabsf(v[i] - med);
  float m = medianOf(d, n);
  free(d);
  return 1.4826f * m;
}

static float percentile98(const float* x, int n) {
  // estimate from a strided copy to keep memory small
  const int m = n < 2000 ? n : 2000;
  float* c = (float*)malloc(m * sizeof(float));
  if (!c) return 0;
  for (int i = 0; i < m; i++) c[i] = x[(long)i * n / m];
  qsort(c, m, sizeof(float), cmpFloat);
  float p = c[(int)(0.98f * (m - 1))];
  free(c);
  return p;
}

int detectPeaks(const float* x, int n, float fs, float minGapS, int* idx, int maxPeaks) {
  float thr = 0.4f * percentile98(x, n);
  int gap = (int)(minGapS * fs), count = 0, last = -gap;
  for (int i = 1; i < n - 1 && count < maxPeaks; i++) {
    if (x[i] > thr && x[i] >= x[i - 1] && x[i] > x[i + 1]) {
      if (i - last >= gap) { idx[count++] = i; last = i; }
      else if (count && x[i] > x[idx[count - 1]]) { idx[count - 1] = i; last = i; }   // keep the taller peak
    }
  }
  return count;
}

float hrFromPeaks(const float* tS, int n, float* quality) {
  *quality = 0;
  if (n < 10) return NAN;
  float* ibi = (float*)malloc((n - 1) * sizeof(float));
  if (!ibi) return NAN;
  int m = 0;
  for (int i = 1; i < n; i++) {
    float d = tS[i] - tS[i - 1];
    if (d > 60.0f / 180 && d < 60.0f / 35) ibi[m++] = d;
  }
  float hr = NAN;
  if (m >= 8) {
    float* tmp = (float*)malloc(m * sizeof(float));
    memcpy(tmp, ibi, m * sizeof(float));
    float med = medianOf(tmp, m);
    free(tmp);
    int near = 0;
    for (int i = 0; i < m; i++) if (fabsf(ibi[i] - med) < 0.15f * med) near++;
    *quality = (float)near / m;
    hr = 60.0f / med;
  }
  free(ibi);
  return hr;
}

float breathingRate(const float* beatT, const float* value, int n) {
  if (n < 20) return NAN;
  // resample at 4 Hz by linear interpolation
  float t0 = beatT[1], t1 = beatT[n - 1];
  int m = (int)((t1 - t0) * 4.0f);
  if (m < 64) return NAN;
  float* s = (float*)malloc(m * sizeof(float));
  if (!s) return NAN;
  int j = 1; float mean = 0;
  for (int i = 0; i < m; i++) {
    float t = t0 + i * 0.25f;
    while (j < n - 1 && beatT[j + 1] < t) j++;
    float f = (t - beatT[j]) / (beatT[j + 1] - beatT[j]);
    s[i] = value[j] + f * (value[j + 1] - value[j]);
    mean += s[i];
  }
  mean /= m;
  for (int i = 0; i < m; i++) s[i] -= mean;
  bandpassFiltFilt(s, m, 0.1f, 0.6f, 4.0f);
  float bestF = NAN, bestP = 0;
  for (float f = 0.12f; f <= 0.58f; f += 0.004f) {       // simple DFT scan over the breathing band
    float re = 0, im = 0, w = 2 * PI * f * 0.25f;
    for (int i = 0; i < m; i++) { re += s[i] * cosf(w * i); im -= s[i] * sinf(w * i); }
    float p = re * re + im * im;
    if (p > bestP) { bestP = p; bestF = f; }
  }
  free(s);
  return isnan(bestF) ? NAN : 60.0f * bestF;
}

void analyseCheck(int16_t* bcgRaw, int nBcg, float* red, float* ir, int nPpg, CheckResult& out) {
  const int bcgSkip = SETTLE_SECONDS * BCG_HZ, ppgSkip = SETTLE_SECONDS * PPG_HZ;
  const int maxBeats = 300;
  int* pk = (int*)malloc(maxBeats * sizeof(int));
  float* pkT = (float*)malloc(maxBeats * sizeof(float));
  float* jT = (float*)malloc(maxBeats * sizeof(float));
  int nJ = 0;
  if (!pk || !pkT || !jT) { free(pk); free(pkT); free(jT); return; }

  // ---------- BCG: band-pass 1-10 Hz, J-wave peaks ----------
  int nb = nBcg - bcgSkip;
  if (nb > BCG_HZ * 20) {
    float* y = (float*)malloc(nb * sizeof(float));
    if (y) {
      for (int i = 0; i < nb; i++) y[i] = -(float)bcgRaw[bcgSkip + i];   // charge amplifier is inverting: flip
      bandpassFiltFilt(y, nb, 1.0f, 10.0f, BCG_HZ);
      nJ = detectPeaks(y, nb, BCG_HZ, 0.33f, pk, maxBeats);
      for (int i = 0; i < nJ; i++) jT[i] = SETTLE_SECONDS + (float)pk[i] / BCG_HZ;
      out.hrBcg = hrFromPeaks(jT, nJ, &out.qBcg);
      free(y);
    }
  }

  // ---------- PPG: HR, SpO2, breathing, pulse feet ----------
  int np = nPpg - ppgSkip;
  float* feetT = (float*)malloc(maxBeats * sizeof(float));
  int nFeet = 0;
  int fingerN = 0;
  for (int i = 0; i < np; i++) if (ir[ppgSkip + i] > FINGER_IR_MIN) fingerN++;
  bool finger = np > 0 && fingerN >= FINGER_PRESENT_FRAC * np;      // no finger / removed mid-check: no PPG values
  if (np > PPG_HZ * 20 && feetT && finger) {
    float* irAc = (float*)malloc(np * sizeof(float));
    float* redAc = (float*)malloc(np * sizeof(float));
    float* irLp = (float*)malloc(np * sizeof(float));
    if (irAc && redAc && irLp) {
      for (int i = 0; i < np; i++) { irAc[i] = ir[ppgSkip + i]; redAc[i] = red[ppgSkip + i]; irLp[i] = ir[ppgSkip + i]; }
      bandpassFiltFilt(irAc, np, 0.5f, 5.0f, PPG_HZ);
      bandpassFiltFilt(redAc, np, 0.5f, 5.0f, PPG_HZ);
      lowpassFiltFilt(irLp, np, 8.0f, PPG_HZ);
      int nP = detectPeaks(irAc, np, PPG_HZ, 0.33f, pk, maxBeats);
      for (int i = 0; i < nP; i++) pkT[i] = SETTLE_SECONDS + (float)pk[i] / PPG_HZ;
      out.hrPpg = hrFromPeaks(pkT, nP, &out.qPpg);

      float* ratios = (float*)malloc(maxBeats * sizeof(float));
      float* bt = (float*)malloc(maxBeats * sizeof(float));
      float* fBase = (float*)malloc(maxBeats * sizeof(float));
      float* fAmp = (float*)malloc(maxBeats * sizeof(float));
      float* fInt = (float*)malloc(maxBeats * sizeof(float));
      int nR = 0, nb2 = 0;
      for (int i = 0; i < nP && ratios && bt && fBase && fAmp && fInt; i++) {
        int p = pk[i], lo = p - (int)(0.35f * PPG_HZ);
        if (lo < 0 || p - lo < 3) continue;
        int m = lo;
        for (int k = lo; k < p; k++) if (irLp[k] < irLp[m]) m = k;      // pulse foot (minimum)
        // intersecting-tangent foot: steepest upstroke meets the minimum level
        float tFoot = SETTLE_SECONDS + (float)m / PPG_HZ;
        if (p - m > 2) {
          int kMax = m; float dMax = 0;
          for (int k = m; k < p; k++) { float dv = (irLp[k + 1] - irLp[k]) * PPG_HZ; if (dv > dMax) { dMax = dv; kMax = k; } }
          if (dMax > 0) {
            float tk = (kMax + 0.5f) / PPG_HZ, vk = 0.5f * (irLp[kMax] + irLp[kMax + 1]);
            tFoot = SETTLE_SECONDS + tk - (vk - irLp[m]) / dMax;
          }
        }
        if (nFeet < maxBeats) feetT[nFeet++] = tFoot;
        float acI = irAc[p] - irAc[m], acR = redAc[p] - redAc[m];
        float dcI = 0, dcR = 0;
        for (int k = m; k <= p; k++) { dcI += ir[ppgSkip + k]; dcR += red[ppgSkip + k]; }
        dcI /= (p - m + 1); dcR /= (p - m + 1);
        if (acI > 0 && acR > 0 && dcI > 0 && dcR > 0) ratios[nR++] = (acR / dcR) / (acI / dcI);
        bt[nb2] = pkT[i]; fBase[nb2] = ir[ppgSkip + m]; fAmp[nb2] = acI;
        fInt[nb2] = nb2 ? (pkT[i] - bt[nb2 - 1]) : NAN; nb2++;
      }
      if (nR >= 5 && out.qPpg >= PPG_Q_MIN) {
        float R = medianOf(ratios, nR);              // sorts ratios in place
        float q25 = ratios[(int)(0.25f * (nR - 1))], q75 = ratios[(int)(0.75f * (nR - 1))];
        if ((q75 - q25) / R <= SPO2_RATIO_SPREAD) {  // beats agree: no movement artefact
          float s = 110.0f - 25.0f * R;               // MAX3010x reference curve; calibrate on the real sensor
          out.spo2 = s < 70 ? 70 : (s > 100 ? 100 : s);
        }
      }
      // breathing: median of baseline, amplitude and interval modulation estimates
      if (nb2 >= 20 && out.qPpg >= PPG_Q_MIN) {
        float est[3]; int ne = 0;
        float r1 = breathingRate(bt, fBase, nb2); if (!isnan(r1)) est[ne++] = r1;
        float r2 = breathingRate(bt, fAmp, nb2);  if (!isnan(r2)) est[ne++] = r2;
        fInt[0] = fInt[1];
        float r3 = breathingRate(bt, fInt, nb2);  if (!isnan(r3)) est[ne++] = r3;
        if (ne >= 2) {
          float lo = est[0], hi = est[0];
          for (int i = 1; i < ne; i++) { lo = fminf(lo, est[i]); hi = fmaxf(hi, est[i]); }
          if (hi - lo <= RR_AGREE_BPM) out.rr = medianOf(est, ne);   // estimates disagree: no breathing value
        }
      }
      free(ratios); free(bt); free(fBase); free(fAmp); free(fInt);
    }
    free(irAc); free(redAc); free(irLp);
  }

  // ---------- fuse heart rate (same rule as pipeline.fuse_hr) ----------
  bool okB = !isnan(out.hrBcg) && out.qBcg >= 0.6f, okP = !isnan(out.hrPpg) && out.qPpg >= 0.6f;
  if (okB && okP) {
    if (fabsf(out.hrBcg - out.hrPpg) <= 5) { out.hr = 0.5f * (out.hrBcg + out.hrPpg); out.hrSource = "both"; }
    else if (out.qBcg > out.qPpg) { out.hr = out.hrBcg; out.hrSource = "bcg"; }
    else { out.hr = out.hrPpg; out.hrSource = "ppg"; }
  } else if (okB) { out.hr = out.hrBcg; out.hrSource = "bcg"; }
  else if (okP) { out.hr = out.hrPpg; out.hrSource = "ppg"; }

  // ---------- PTT (secondary): J-wave -> next pulse foot, 100-400 ms ----------
  if (okB && okP && nJ >= 10 && nFeet >= 10) {
    float* d = (float*)malloc(nJ * sizeof(float)); int nd = 0;
    for (int i = 0; i < nJ && d; i++) {
      for (int k = 0; k < nFeet; k++) {
        float dt = feetT[k] - jT[i];
        if (dt > 0.10f && dt < 0.40f) { d[nd++] = dt; break; }
      }
    }
    if (nd >= 10) out.pttMs = 1000.0f * medianOf(d, nd);
    free(d);
  }
  free(feetT); free(pk); free(pkT); free(jT);
}
