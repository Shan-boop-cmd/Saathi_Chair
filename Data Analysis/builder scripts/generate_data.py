"""
SAATHI chair - synthetic patient generator, v3 (Checkpoint 1)

Simulates what the chair would record once per day for elderly heart-failure patients.
Signals (v3.1): seated weight, foot bioimpedance, resting heart rate (BCG+PPG fused), SpO2,
respiratory rate (from the MAX30102 PPG signal), 3 yes/no symptom answers, time of test,
daily sit-down count (optional activity signal), and pulse transit time PTT (seat BCG peak ->
finger PPG foot) as a SECONDARY signal with weak, uncertain effect. Noise is kept simple in this version.

Design goal: realistic difficulty, NOT a target score. Effect sizes are kept modest, many
events are subtle, and the data contains things that mimic worsening (festivals, fever,
heavy clothing) or shift the baseline (diuretic dose change).

Outputs (./data):
  daily.csv     what the chair records; the detector sees ONLY this file
  truth.csv     ground truth per day (worsening window, confounders); used for scoring only
  patients.csv  per-patient profile and the hidden event parameters
Run: python generate_data.py [--n 120 --days 60 --seed 26]
"""
import argparse, os
import numpy as np
import pandas as pd

ap = argparse.ArgumentParser()
ap.add_argument("--n", type=int, default=120)
ap.add_argument("--days", type=int, default=60)
ap.add_argument("--seed", type=int, default=26)
args = ap.parse_args()
rng = np.random.default_rng(args.seed)
N, D = args.n, args.days
BASELINE_DAYS = 14            # chair learns "normal" first; admissions only after this + max lead
DIURNAL_KG_PER_H = 0.07       # weight rises through the day (food, water): ~0.8 kg from 7am to 7pm
os.makedirs("data", exist_ok=True)

def U(a, b):
    return float(rng.uniform(a, b))

daily, truth, meta = [], [], []
for p in range(N):
    pid = f"P{p+1:03d}"
    sex = str(rng.choice(["F", "M"]))
    age = int(rng.integers(60, 86))

    # ---------- personal baseline: every patient's "normal" is different ----------
    w0   = float(np.clip(rng.normal(58 if sex == "F" else 66, 10), 38, 100))  # seated weight, kg
    z0   = float(rng.normal(560 if sex == "F" else 480, 50))                  # foot-to-foot |Z| at 50 kHz, ohm
    hr0  = float(np.clip(rng.normal(74, 9), 55, 100))                         # resting HR, bpm
    sp0  = float(np.clip(rng.normal(95.5, 1.3), 92, 99))                      # SpO2, %
    rr0  = float(np.clip(rng.normal(16, 2), 12, 22))                          # respiratory rate from PPG, breaths/min
    ptt0 = float(rng.normal(230, 20))                                         # BCG->PPG pulse transit time, ms
    sit0 = U(5, 14)                                                           # sit-downs per day
    has_implant = bool(rng.random() < 0.25)                                   # pacemaker/ICD: impedance off
    morning = rng.random() < 0.7
    pref_h = U(7, 10) if morning else U(16, 19)                               # usual test time
    p_report = U(0.2, 0.8)       # how readily this person admits symptoms (elderly under-report)
    p_false_yes = U(0.02, 0.08)  # "yes" on a normal day (misunderstanding, other illness)
    p_miss = U(0.02, 0.15)       # how often they skip the check

    # ---------- worsening event ----------
    has_event = bool(rng.random() < 0.6)
    adm = lead = None
    phen = "stable"
    eff = dict(wt=0.0, z=0.0, hr=0.0, sp=0.0, rr=0.0, ptt=0.0, act=0.0, miss=0.0)
    if has_event:
        lead = int(rng.integers(3, 11))                                       # worsening starts 3-10 days before
        adm = int(rng.integers(30, D + 1))   # admission day >= 30, so every patient has >= 30 days of data (problem rule)
        r = rng.random()
        if r < 0.6:
            phen = "congestive"          # classic fluid build-up
            eff = dict(wt=U(0.8, 3.5), z=U(0.03, 0.09), hr=U(3, 12), sp=U(0.0, 2.0), rr=U(1.5, 5.0), ptt=U(-15, 5), act=U(0.1, 0.4), miss=U(0, 0.25))
        elif r < 0.8:
            phen = "no_weight_gain"      # admission without weight gain (named in the problem's evidence)
            eff = dict(wt=U(0.0, 0.4), z=U(0.0, 0.03), hr=U(5, 15), sp=U(1.0, 3.0), rr=U(2.0, 6.0), ptt=U(-15, 5), act=U(0.2, 0.5), miss=U(0, 0.25))
        else:
            phen = "subtle"              # barely visible before admission; a realistic share of events is missed
            eff = dict(wt=U(0.2, 0.9), z=U(0.005, 0.025), hr=U(1, 4), sp=U(0.0, 0.7), rr=U(0.0, 1.5), ptt=U(-5, 3), act=U(0.0, 0.15), miss=U(0, 0.1))
    ramp = U(1.0, 2.0)                   # how sharply it accelerates near admission

    # ---------- confounders and real-life variation ----------
    conf = [""] * D
    w_c = np.zeros(D); z_c = np.zeros(D); hr_c = np.zeros(D); sp_c = np.zeros(D); rr_c = np.zeros(D)
    act_c = np.ones(D); breath_c = np.zeros(D)
    for _ in range(int(rng.integers(0, 4))):                 # festival / salty meal / wedding food: 2-3 days
        s = int(rng.integers(0, D - 3)); L = int(rng.integers(2, 4))
        w_c[s:s+L] += U(0.5, 1.5); z_c[s:s+L] -= U(0.0, 0.02)
        for k in range(s, min(D, s + L)): conf[k] = "festival"
    if rng.random() < 0.3:                                   # fever / infection: 3-6 days
        s = int(rng.integers(0, D - 6)); L = int(rng.integers(3, 7))
        hr_c[s:s+L] += U(6, 15); sp_c[s:s+L] -= U(0.5, 1.5); rr_c[s:s+L] += U(2, 4); breath_c[s:s+L] = 0.35; act_c[s:s+L] = 0.7
        for k in range(s, min(D, s + L)): conf[k] = "infection"
    if rng.random() < 0.25:                                  # diuretic dose increased: lasting baseline shift
        s = int(rng.integers(5, D - 5)); dw = U(0.5, 1.5)
        w_c[s:] -= dw; z_c[s:] += U(0.02, 0.05)
        for k in range(s, min(D, s + 3)): conf[k] = conf[k] or "diuretic_change"
    for k in range(D):                                       # heavy shawl / sweater on cold days
        if rng.random() < 0.03:
            w_c[k] += U(0.3, 1.0); conf[k] = conf[k] or "heavy_clothing"
    w_drift = np.cumsum(rng.normal(0, 0.04, D))              # slow real weight drift
    hr_drift = np.cumsum(rng.normal(0, 0.3, D))              # slow resting-HR drift (sleep, season, meds)

    last = adm if has_event else D
    start = (adm - lead) if has_event else None
    for d in range(last):
        sev = 0.0
        if has_event and d >= start:
            sev = ((d - start + 1) / lead) ** ramp           # 0 before onset, reaches 1 on the day before admission
        row = dict(patient_id=pid, day=d)
        missed = rng.random() < p_miss + eff["miss"] * sev   # sicker people skip more
        row["missed"] = int(missed)
        if missed:
            for c in ["test_hour", "weight_kg", "foot_z_ohm", "rest_hr_bpm", "spo2_pct", "resp_rate_bpm", "ptt_ms",
                      "q_orthopnea", "q_breathless", "q_swelling"]:
                row[c] = np.nan
        else:
            h = U(7, 21) if rng.random() < 0.15 else float(np.clip(rng.normal(pref_h, 1.0), 6, 22))
            row["test_hour"] = h
            row["weight_kg"] = (w0 + w_drift[d] + w_c[d] + eff["wt"] * sev
                                + DIURNAL_KG_PER_H * (h - 7) + rng.normal(0, 0.3))
            z = z0 * (1 + z_c[d] - eff["z"] * sev) * (1 + rng.normal(0, 0.025))
            row["foot_z_ohm"] = np.nan if (has_implant or rng.random() < 0.05) else z   # 5%: feet not bare / poor contact
            row["rest_hr_bpm"] = (np.nan if rng.random() < 0.02 else                     # both BCG and PPG failed
                                  hr0 + hr_drift[d] + hr_c[d] + eff["hr"] * sev + rng.normal(0, 4))
            finger_ok = rng.random() >= 0.06                                             # finger moved -> no PPG values
            row["spo2_pct"] = (min(100.0, sp0 + sp_c[d] - eff["sp"] * sev + rng.normal(0, 1.0)) if finger_ok else np.nan)
            row["resp_rate_bpm"] = (float(np.clip(rr0 + rr_c[d] + eff["rr"] * sev + rng.normal(0, 2.5), 8, 35)) if finger_ok else np.nan)  # PPG-derived RR over 90 s is imprecise
            bcg_ok = rng.random() >= 0.03                                                # PTT needs both BCG and PPG
            row["ptt_ms"] = (ptt0 + eff["ptt"] * sev + rng.normal(0, 8) if (finger_ok and bcg_ok) else np.nan)
            sym = p_report * sev
            row["q_orthopnea"]  = int(rng.random() < p_false_yes + sym * (0.2 if phen == "subtle" else 0.8))
            row["q_breathless"] = int(rng.random() < p_false_yes + breath_c[d] + sym * (0.3 if phen == "subtle" else 0.9))
            row["q_swelling"]   = int(rng.random() < p_false_yes + sym * (0.7 if phen == "congestive" else 0.15))
        # the load cells count sit-downs whether or not the check was done
        row["sit_count"] = int(rng.poisson(sit0 * act_c[d] * (1 - eff["act"] * sev)))
        daily.append(row)
        truth.append(dict(patient_id=pid, day=d, worsening=int(sev > 0), severity=round(sev, 3),
                          days_to_admission=(adm - d) if has_event else np.nan, confounder=conf[d]))
    meta.append(dict(patient_id=pid, sex=sex, age=age, has_event=int(has_event), admission_day=adm,
                     lead_days=lead, phenotype=phen, has_implant=int(has_implant),
                     usual_test_hour=round(pref_h, 1), miss_rate=round(p_miss, 3), symptom_report=round(p_report, 2),
                     weight_gain_kg=round(eff["wt"], 2), z_drop_frac=round(eff["z"], 3), hr_rise_bpm=round(eff["hr"], 1),
                     spo2_drop=round(eff["sp"], 2), rr_rise=round(eff["rr"], 1), ptt_change_ms=round(eff["ptt"], 1), activity_drop_frac=round(eff["act"], 2)))

df = pd.DataFrame(daily)
cols = ["patient_id", "day", "missed", "test_hour", "weight_kg", "foot_z_ohm", "rest_hr_bpm", "spo2_pct",
        "resp_rate_bpm", "ptt_ms", "q_orthopnea", "q_breathless", "q_swelling", "sit_count"]
df = df[cols]
df["test_hour"] = df["test_hour"].round(2)
df[["weight_kg", "rest_hr_bpm", "spo2_pct", "resp_rate_bpm", "ptt_ms"]] = df[["weight_kg", "rest_hr_bpm", "spo2_pct", "resp_rate_bpm", "ptt_ms"]].round(1)
df["foot_z_ohm"] = df["foot_z_ohm"].round(0)
df.to_csv("data/daily.csv", index=False)
pd.DataFrame(truth).to_csv("data/truth.csv", index=False)
pm = pd.DataFrame(meta)
pm.to_csv("data/patients.csv", index=False)
print(f"{N} patients, {len(df)} patient-days, {pm.has_event.sum()} admissions: "
      + ", ".join(f"{k} {v}" for k, v in pm[pm.has_event == 1].phenotype.value_counts().items())
      + f"; {pm.has_implant.sum()} with implants")
