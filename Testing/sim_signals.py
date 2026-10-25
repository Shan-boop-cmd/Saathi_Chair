"""
SAATHI chair - raw signal simulator (Checkpoint 2).

Turns one day's true physiological state (a row of data/daily.csv) into the raw signals the chair's
sensors would record during the 90-second check:
  - 4 leg load cells (HX711, 80 Hz, kg per cell)                -> weight
  - seat PVDF film after the charge amplifier (500 Hz, volts)    -> BCG heartbeats
  - MAX30102 red + IR PPG (100 Hz, raw counts)                   -> HR, SpO2, respiratory rate
  - AD5933 foot impedance readings at 50 kHz (20 readings, ohm) -> foot fluid
PTT is built in physically: each finger pulse arrives PTT ms after the BCG J-wave of the same beat.
Noise is kept simple in this version (white noise + a settling movement at the start).
"""
import numpy as np

FS_BCG, FS_PPG, FS_LC = 500, 100, 80
DURATION = 90.0          # seconds of measurement
SETTLE = 5.0             # first seconds: person shifting, footrest unfolding
CHAIR_TARE = 9.5         # kg, chair + footrest weight on the 4 load cells (auto-zeroed when empty)


def _gauss(t, mu, sig):
    return np.exp(-0.5 * ((t - mu) / sig) ** 2)


def beat_times(hr, rr, rng, duration=DURATION):
    """Heartbeat (R-wave) times with mild heart-rate variability and breathing-linked variation."""
    base = 60.0 / hr
    f_resp = rr / 60.0
    times, t = [], rng.uniform(0, base)
    while t < duration + 1.0:
        times.append(t)
        t += base * (1 + 0.025 * rng.standard_normal() + 0.02 * np.sin(2 * np.pi * f_resp * t))
    return np.array(times)


def respiration(t, rr, phase):
    return np.sin(2 * np.pi * (rr / 60.0) * t + phase)


def simulate_bcg(beats, rr, rng, phase):
    t = np.arange(0, DURATION, 1 / FS_BCG)
    resp = respiration(t, rr, phase)
    x = np.zeros_like(t)
    # one BCG complex per beat: H, I, J, K, L waves; the J-wave peaks ~250 ms after the R-wave
    waves = [(0.18, 0.012, 0.30), (0.21, 0.010, -0.60), (0.25, 0.012, 1.00), (0.30, 0.014, -0.50), (0.35, 0.020, 0.20)]
    amp_beats = 1 + 0.15 * np.interp(beats, t, resp)          # breathing modulates BCG amplitude
    for b, a in zip(beats, amp_beats):
        for off, sig, amp in waves:
            mu = b + off
            lo, hi = int((mu - 5 * sig) * FS_BCG), int((mu + 5 * sig) * FS_BCG)
            lo, hi = max(lo, 0), min(hi, len(t))
            if lo < hi:
                x[lo:hi] += a * amp * _gauss(t[lo:hi], mu, sig)
    x += 0.8 * resp                                            # PVDF also feels breathing (removed by the filter)
    x += 0.12 * rng.standard_normal(len(t))
    settle = t < SETTLE                                        # sitting down / adjusting
    x[settle] += 3.0 * rng.standard_normal(settle.sum())
    j_times = beats + 0.25
    return t, x, j_times


def simulate_ppg(j_times, ptt_ms, spo2, rr, rng, phase):
    t = np.arange(0, DURATION, 1 / FS_PPG)
    resp = respiration(t, rr, phase)
    feet = j_times + ptt_ms / 1000.0                           # pulse arrives at the finger PTT after the J-wave
    pulse = np.zeros_like(t)
    for f in feet:
        tau = t - f
        rise = (tau >= 0) & (tau < 0.15)
        pulse[rise] += 0.5 * (1 - np.cos(np.pi * tau[rise] / 0.15))
        fall = tau >= 0.15
        pulse[fall] += np.exp(-(tau[fall] - 0.15) / 0.30) + 0.25 * _gauss(tau[fall], 0.40, 0.05)
    pulse -= pulse.mean()
    R = (110.0 - spo2) / 25.0                                  # standard MAX3010x calibration: SpO2 = 110 - 25 R
    dc_ir, dc_red = 50000.0, 30000.0
    pi_ir = 0.02                                               # perfusion: AC is ~2% of DC
    ac_ir = pi_ir * dc_ir
    ac_red = R * pi_ir * dc_red
    am = 1 + 0.10 * resp                                       # breathing modulates pulse amplitude (RIAV)
    bw = 1 + 0.004 * resp                                      # ... and the baseline (RIIV)
    ir = dc_ir * bw + ac_ir * am * pulse / np.ptp(pulse) * 1.0
    red = dc_red * bw + ac_red * am * pulse / np.ptp(pulse) * 1.0
    ir += 0.01 * ac_ir * rng.standard_normal(len(t))
    red += 0.01 * ac_red * rng.standard_normal(len(t))
    settle = t < SETTLE
    ir[settle] += 0.5 * ac_ir * rng.standard_normal(settle.sum())
    red[settle] += 0.5 * ac_red * rng.standard_normal(settle.sum())
    return t, red, ir, feet


def simulate_loadcells(weight, beats, rr, rng, phase):
    t = np.arange(0, DURATION, 1 / FS_LC)
    resp = respiration(t, rr, phase)
    share = rng.dirichlet([20, 20, 15, 15])                    # how the body weight spreads over the 4 legs
    on = np.clip(t / 2.0, 0, 1)                                # sitting down over the first 2 s
    total = CHAIR_TARE + weight * on
    total[t < 3] += 1.5 * np.sin(2 * np.pi * 1.5 * t[t < 3]) * np.exp(-t[t < 3])   # overshoot as they settle
    cells = []
    for k in range(4):
        sway = 0.15 * resp * (1 if k < 2 else -1)              # breathing shifts weight front/back; sum unchanged
        c = total * (share[k] if k else share[k]) + sway + 0.05 * rng.standard_normal(len(t))
        cells.append(c)
    return t, np.vstack(cells)


def simulate_impedance(z, rng, n=20):
    return z * (1 + 0.004 * rng.standard_normal(n))


def simulate_session(row, rng):
    """row: one measured day from daily.csv. Returns dict of raw signals + the truth used."""
    hr = row["rest_hr_bpm"]
    finger_ok = not np.isnan(row["spo2_pct"])
    bcg_ok = (not np.isnan(hr)) and not (finger_ok and np.isnan(row["ptt_ms"]))
    rr = row["resp_rate_bpm"] if finger_ok else 16.0
    phase = rng.uniform(0, 2 * np.pi)
    out = {"truth": row}
    hr_sim = hr if not np.isnan(hr) else 70.0
    beats = beat_times(hr_sim, rr, rng)
    if bcg_ok:
        out["bcg_t"], out["bcg"], j = simulate_bcg(beats, rr, rng, phase)
    else:
        out["bcg_t"], out["bcg"] = np.arange(0, DURATION, 1 / FS_BCG), 3.0 * rng.standard_normal(int(DURATION * FS_BCG))
        j = beats + 0.25
    if finger_ok:
        ptt = row["ptt_ms"] if not np.isnan(row["ptt_ms"]) else 230.0
        out["ppg_t"], out["red"], out["ir"], _ = simulate_ppg(j, ptt, row["spo2_pct"], rr, rng, phase)
    out["lc_t"], out["lc"] = simulate_loadcells(row["weight_kg"], beats, rr, rng, phase)
    out["tare_kg"] = CHAIR_TARE + 0.02 * rng.standard_normal()   # last empty-chair reading
    if not np.isnan(row["foot_z_ohm"]):
        out["z_readings"] = simulate_impedance(row["foot_z_ohm"], rng)
    return out
