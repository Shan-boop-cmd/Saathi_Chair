"""
SAATHI chair - signal processing pipeline (Checkpoint 2).

Raw 90-second sensor signals -> one row of daily numbers. The same steps will be ported to the
ESP32 firmware; this Python version is the reference implementation.

  weight_kg      steadiest 10 s of the summed load cells, minus the empty-chair tare
  foot_z_ohm     median of the AD5933 readings, rejected if contact is poor
  hr_bcg_bpm     seat BCG: band-pass 1-10 Hz, J-wave peaks, median beat interval
  hr_ppg_bpm     finger PPG: band-pass 0.5-5 Hz, pulse peaks, median beat interval
  rest_hr_bpm    BCG and PPG fused: average if they agree within 5 bpm, else the better-quality one
  spo2_pct       per-beat red/IR ratio R, SpO2 = 110 - 25 R (MAX3010x reference curve; real sensor needs calibration)
  resp_rate_bpm  breathing from the PPG: baseline, amplitude and beat-interval modulation, spectral peak, median of three
  ptt_ms         SECONDARY: BCG J-wave -> next PPG pulse foot, median over beats
"""
import numpy as np
from scipy.signal import butter, sosfiltfilt, find_peaks, welch

SKIP = 6.0   # ignore the settling seconds at the start


def bandpass(x, fs, lo, hi, order=3):
    sos = butter(order, [lo, hi], btype="band", fs=fs, output="sos")
    return sosfiltfilt(sos, x)


def hr_from_peaks(peaks_s, lo=35, hi=180):
    """Median beat interval -> bpm, plus a quality score (share of intervals near the median)."""
    if len(peaks_s) < 10:
        return np.nan, 0.0
    ibi = np.diff(peaks_s)
    ibi = ibi[(ibi > 60 / hi) & (ibi < 60 / lo)]
    if len(ibi) < 8:
        return np.nan, 0.0
    med = np.median(ibi)
    quality = float(np.mean(np.abs(ibi - med) < 0.15 * med))
    return 60.0 / med, quality


def detect_beats(x, fs, min_gap_s=0.33):
    """Adaptive peak detection: peaks above 40% of the robust amplitude, at least min_gap apart."""
    scale = np.percentile(x, 98)
    pk, _ = find_peaks(x, height=0.4 * scale, distance=int(min_gap_s * fs))
    return pk


def process_weight(lc_t, lc, tare):
    total = lc.sum(axis=0)
    fs = 1 / (lc_t[1] - lc_t[0])
    win = int(10 * fs)
    start = int(SKIP * fs)
    best, best_sd = None, np.inf
    for i in range(start, len(total) - win, int(fs)):
        sd = total[i:i + win].std()
        if sd < best_sd:
            best, best_sd = i, sd
    if best is None or best_sd > 0.5:
        return np.nan, False
    return float(np.median(total[best:best + win]) - tare), True


def process_impedance(z):
    z = np.asarray(z)
    med = np.median(z)
    ok = (200 < med < 1500) and (np.std(z) / med < 0.05)    # outside this: feet not bare / not on the plates
    return (float(med), True) if ok else (np.nan, False)


def process_bcg(t, x):
    fs = 1 / (t[1] - t[0])
    keep = t >= SKIP
    y = bandpass(x, fs, 1.0, 10.0)[keep]
    pk = detect_beats(y, fs)
    peaks_s = t[keep][pk]
    hr, q = hr_from_peaks(peaks_s)
    return hr, q, peaks_s


def process_ppg(t, red, ir):
    fs = 1 / (t[1] - t[0])
    keep = t >= SKIP
    tt = t[keep]
    ir_ac = bandpass(ir, fs, 0.5, 5.0)[keep]
    red_ac = bandpass(red, fs, 0.5, 5.0)[keep]
    pk = detect_beats(ir_ac, fs)
    hr, q = hr_from_peaks(tt[pk])
    # pulse feet: the minimum of the lightly smoothed raw IR in the 350 ms before each systolic peak
    # (the band-passed signal is not used here: high-pass filtering pulls the minimum earlier and biases PTT)
    sos = butter(3, 8.0, btype="low", fs=fs, output="sos")
    ir_lp = sosfiltfilt(sos, ir)[keep]
    feet, feet_t = [], []
    for p in pk:
        lo = max(0, p - int(0.35 * fs))
        if p - lo > 2:
            m = lo + int(np.argmin(ir_lp[lo:p]))
            feet.append(m)
            # intersecting-tangent foot (standard PTT method): tangent at the steepest upstroke meets the minimum level
            if p - m > 2:
                dv = np.diff(ir_lp[m:p + 1]) * fs
                k = int(np.argmax(dv))
                tk = tt[m + k] + 0.5 / fs
                vk = 0.5 * (ir_lp[m + k] + ir_lp[m + k + 1])
                feet_t.append(tk - (vk - ir_lp[m]) / dv[k] if dv[k] > 0 else tt[m])
            else:
                feet_t.append(tt[m])
    feet = np.array(feet, int)
    # SpO2 from per-beat AC/DC ratio of red vs IR
    ratios = []
    ir_dc, red_dc = ir[keep], red[keep]
    for f, p in zip(feet, pk[-len(feet):] if len(feet) else []):
        ac_i, ac_r = ir_ac[p] - ir_ac[f], red_ac[p] - red_ac[f]
        dc_i, dc_r = ir_dc[f:p + 1].mean(), red_dc[f:p + 1].mean()
        if ac_i > 0 and ac_r > 0:
            ratios.append((ac_r / dc_r) / (ac_i / dc_i))
    spo2 = float(np.clip(110 - 25 * np.median(ratios), 70, 100)) if len(ratios) >= 5 else np.nan
    rr = respiratory_rate(tt, ir[keep], ir_ac, pk, feet, fs)
    return dict(hr=hr, q=q, spo2=spo2, rr=rr, feet_s=np.array(feet_t))


def respiratory_rate(tt, ir_raw, ir_ac, pk, feet, fs):
    """Breathing rate from three PPG modulations (baseline, amplitude, beat interval), median of three."""
    if len(pk) < 20 or len(feet) < 20:
        return np.nan
    n = min(len(pk), len(feet))
    pk, feet = pk[-n:], feet[-n:]
    bt = tt[pk]
    feats = {
        "baseline": ir_raw[feet],                         # RIIV
        "amplitude": ir_ac[pk] - ir_ac[feet],             # RIAV
        "interval": np.r_[np.nan, np.diff(bt)],           # RIFV
    }
    grid = np.arange(bt[1], bt[-1], 0.25)                 # resample at 4 Hz
    est = []
    for k, v in feats.items():
        m = ~np.isnan(v)
        if m.sum() < 20:
            continue
        s = np.interp(grid, bt[m], v[m])
        s = s - s.mean()
        s = bandpass(s, 4.0, 0.1, 0.6, order=2)           # 6-36 breaths/min
        f, p = welch(s, fs=4.0, nperseg=min(len(s), 256), nfft=2048)
        band = (f >= 0.12) & (f <= 0.58)
        if band.any():
            est.append(60 * f[band][np.argmax(p[band])])
    return float(np.median(est)) if est else np.nan


def process_ptt(j_s, feet_s):
    """SECONDARY signal: J-wave of the BCG to the foot of the next finger pulse (100-400 ms)."""
    if len(j_s) < 10 or len(feet_s) < 10:
        return np.nan
    d = []
    for j in j_s:
        nxt = feet_s[(feet_s > j + 0.10) & (feet_s < j + 0.40)]
        if len(nxt):
            d.append(nxt[0] - j)
    return float(1000 * np.median(d)) if len(d) >= 10 else np.nan


def fuse_hr(hr_b, q_b, hr_p, q_p):
    if np.isnan(hr_b) and np.isnan(hr_p):
        return np.nan, "none"
    if np.isnan(hr_p) or q_p < 0.6:
        return (hr_b, "bcg") if q_b >= 0.6 else (np.nan, "none")
    if np.isnan(hr_b) or q_b < 0.6:
        return hr_p, "ppg"
    if abs(hr_b - hr_p) <= 5:
        return (hr_b + hr_p) / 2, "both"
    return (hr_b, "bcg") if q_b > q_p else (hr_p, "ppg")


def process_session(s):
    out = {}
    out["weight_kg"], _ = process_weight(s["lc_t"], s["lc"], s["tare_kg"])
    out["foot_z_ohm"] = process_impedance(s["z_readings"])[0] if "z_readings" in s else np.nan
    hr_b, q_b, j = process_bcg(s["bcg_t"], s["bcg"])
    if "ir" in s:
        p = process_ppg(s["ppg_t"], s["red"], s["ir"])
    else:
        p = dict(hr=np.nan, q=0.0, spo2=np.nan, rr=np.nan, feet_s=np.array([]))
    out["hr_bcg_bpm"], out["hr_ppg_bpm"] = hr_b, p["hr"]
    out["rest_hr_bpm"], out["hr_source"] = fuse_hr(hr_b, q_b, p["hr"], p["q"])
    out["spo2_pct"], out["resp_rate_bpm"] = p["spo2"], p["rr"]
    out["ptt_ms"] = process_ptt(j, p["feet_s"]) if (q_b >= 0.6 and p["q"] >= 0.6) else np.nan
    return out
