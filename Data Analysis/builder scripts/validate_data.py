"""
SAATHI - Checkpoint 1 data validation.

Part A: rule checks against the problem statement and physiology (PASS/FAIL).
Part B: does each signal actually change before admission, by phenotype?
Part C: difficulty check - two deliberately crude reference detectors with rules fixed in advance.
        If even these score very high, the data is unrealistically easy and the generator is fixed
        (realism, not a target number). Our real detector is built and scored in Checkpoint 3.
Writes reports/validation_report.md and figures to reports/.
"""
import os
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

os.makedirs("reports", exist_ok=True)
d = pd.read_csv("data/daily.csv")
t = pd.read_csv("data/truth.csv")
m = pd.read_csv("data/patients.csv")
dt = d.merge(t, on=["patient_id", "day"])
BASE = 14
INK, INK2, GRID = "#0b0b0b", "#52514e", "#e4e3df"
S1, S2, S3, S4 = "#2a78d6", "#eb6834", "#1baf7a", "#eda100"
plt.rcParams.update({"font.size": 10, "axes.edgecolor": "#b8b7b1", "axes.labelcolor": INK2,
                     "xtick.color": INK2, "ytick.color": INK2, "axes.spines.top": False,
                     "axes.spines.right": False, "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.6})

lines = ["# Checkpoint 1 — synthetic data validation", ""]
checks = []
def check(name, ok, detail):
    checks.append((name, "PASS" if ok else "FAIL", detail))

# ---------------- Part A: rule checks ----------------
days_per = d.groupby("patient_id").day.count()
ev = m[m.has_event == 1]
check("At least 20 patients", len(m) >= 20, f"{len(m)} patients")
check("At least 30 days of data per patient", days_per.min() >= 30, f"min {days_per.min()}, max {days_per.max()} days")
check("Worsening starts 3-10 days before admission", ev.lead_days.between(3, 10).all(),
      f"lead days {int(ev.lead_days.min())}-{int(ev.lead_days.max())}, median {ev.lead_days.median():.0f}")
last_day = d.groupby("patient_id").day.max()
no_after = all(last_day[r.patient_id] == r.admission_day - 1 for r in ev.itertuples())
check("No data on or after admission day", no_after, "data ends the day before admission for every event patient")
wwin = t[t.worsening == 1].groupby("patient_id").day.agg(["min", "max", "count"])
win_ok = all(wwin.loc[r.patient_id, "count"] == r.lead_days for r in ev.itertuples())
check("Worsening window length matches lead days", win_ok, "truth.csv window = lead days for every event")
check("Events only after the 14-day baseline", (ev.admission_day - ev.lead_days >= BASE).all(),
      f"earliest worsening onset day {int((ev.admission_day - ev.lead_days).min())}")
rng_ok = {
    "weight_kg": d.weight_kg.between(30, 125), "foot_z_ohm": d.foot_z_ohm.between(250, 900),
    "rest_hr_bpm": d.rest_hr_bpm.between(40, 140), "spo2_pct": d.spo2_pct.between(80, 100),
    "test_hour": d.test_hour.between(6, 22), "sit_count": d.sit_count.between(0, 40),
    "resp_rate_bpm": d.resp_rate_bpm.between(8, 35), "ptt_ms": d.ptt_ms.between(120, 350)}
for c, s in rng_ok.items():
    v = d[c].dropna()
    check(f"Plausible range: {c}", s[d[c].notna()].all(), f"{v.min():.1f} to {v.max():.1f}")
miss = d.missed.mean()
check("Missed-check rate realistic (2-25%)", 0.02 <= miss <= 0.25, f"{miss:.1%} of days missed")
imp = m[m.has_implant == 1].patient_id
check("Impedance off for pacemaker/ICD patients", d[d.patient_id.isin(imp)].foot_z_ohm.isna().all(),
      f"{len(imp)} implant patients, 0 impedance readings")
check("Some admissions without weight gain", (ev.phenotype == "no_weight_gain").sum() > 0,
      f"{(ev.phenotype == 'no_weight_gain').sum()} of {len(ev)} admissions")
check("Confounders present", t.confounder.notna().sum() > 0,
      ", ".join(f"{k}: {v} days" for k, v in t.confounder.value_counts().items()))
check("Respiratory rate (primary) and PTT (secondary) present", all(c in d.columns for c in ["ptt_ms", "resp_rate_bpm"]),
      "columns: " + ", ".join(d.columns[2:]))
# diurnal effect recoverable: within-patient slope of weight on test hour, normal days only
nd = dt[(dt.worsening == 0) & dt.weight_kg.notna()].copy()
nd["w_c"] = nd.weight_kg - nd.groupby("patient_id").weight_kg.transform("mean")
nd["h_c"] = nd.test_hour - nd.groupby("patient_id").test_hour.transform("mean")
slope = (nd.w_c * nd.h_c).sum() / (nd.h_c ** 2).sum()
check("Time-of-day weight effect present", 0.04 < slope < 0.10, f"{slope:.3f} kg per hour (set to 0.07)")

lines += ["## A. Rule checks", "", "| Check | Result | Detail |", "| --- | --- | --- |"]
lines += [f"| {a} | {b} | {c} |" for a, b, c in checks]
n_fail = sum(1 for c in checks if c[1] == "FAIL")
lines += ["", f"**{len(checks) - n_fail} of {len(checks)} checks pass.**", ""]

# ---------------- Part B: signal change before admission ----------------
def base_stats(g):
    b = g[g.day < BASE]
    return pd.Series({c: b[c].median() for c in ["weight_kg", "foot_z_ohm", "rest_hr_bpm", "spo2_pct", "resp_rate_bpm", "ptt_ms", "sit_count"]})
bs = d.groupby("patient_id").apply(base_stats)
rows = []
for r in ev.itertuples():
    g = dt[(dt.patient_id == r.patient_id) & (dt.day >= r.admission_day - 3)]
    b = bs.loc[r.patient_id]
    rows.append(dict(phenotype=r.phenotype,
                     weight=g.weight_kg.mean() - b.weight_kg,
                     z_pct=100 * (g.foot_z_ohm.mean() - b.foot_z_ohm) / b.foot_z_ohm if not np.isnan(b.foot_z_ohm) else np.nan,
                     hr=g.rest_hr_bpm.mean() - b.rest_hr_bpm, spo2=g.spo2_pct.mean() - b.spo2_pct, rr=g.resp_rate_bpm.mean() - b.resp_rate_bpm, ptt=g.ptt_ms.mean() - b.ptt_ms,
                     yes=g[["q_orthopnea", "q_breathless", "q_swelling"]].sum(axis=1).mean(),
                     sits_pct=100 * (g.sit_count.mean() - b.sit_count) / b.sit_count))
# stable patients: same window = their last 3 days
for r in m[m.has_event == 0].itertuples():
    g = dt[(dt.patient_id == r.patient_id) & (dt.day >= dt.day.max() - 2)]
    b = bs.loc[r.patient_id]
    rows.append(dict(phenotype="stable", weight=g.weight_kg.mean() - b.weight_kg,
                     z_pct=100 * (g.foot_z_ohm.mean() - b.foot_z_ohm) / b.foot_z_ohm if not np.isnan(b.foot_z_ohm) else np.nan,
                     hr=g.rest_hr_bpm.mean() - b.rest_hr_bpm, spo2=g.spo2_pct.mean() - b.spo2_pct, rr=g.resp_rate_bpm.mean() - b.resp_rate_bpm, ptt=g.ptt_ms.mean() - b.ptt_ms,
                     yes=g[["q_orthopnea", "q_breathless", "q_swelling"]].sum(axis=1).mean(),
                     sits_pct=100 * (g.sit_count.mean() - b.sit_count) / b.sit_count))
sh = pd.DataFrame(rows)
order = ["congestive", "no_weight_gain", "subtle", "stable"]
tab = sh.groupby("phenotype").median().reindex(order)
cnt = sh.phenotype.value_counts().reindex(order)
lines += ["## B. How much each signal moves in the last 3 days before admission",
          "", "Median change from the patient's own 14-day baseline. Stable patients: their last 3 days, for comparison.", "",
          "| Group | Patients | Weight (kg) | Foot impedance (%) | Resting HR (bpm) | SpO₂ (points) | Resp. rate (/min) | PTT (ms, secondary) | 'Yes' answers per day (of 3) | Sit-downs (%) |",
          "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
for ph in order:
    r = tab.loc[ph]
    lines.append(f"| {ph} | {cnt[ph]} | {r.weight:+.2f} | {r.z_pct:+.1f} | {r.hr:+.1f} | {r.spo2:+.2f} | {r.rr:+.1f} | {r.ptt:+.1f} | {r.yes:.2f} | {r.sits_pct:+.0f} |")
lines += ["", "Weight changes include the time-of-day effect and festivals, so stable patients are not exactly zero.", ""]

# ---------------- Part C: difficulty check with crude reference detectors ----------------
def evaluate(alert):
    """alert: DataFrame patient_id, day, alert(0/1) for every row of daily.csv. Scored on days >= BASE."""
    a = alert.merge(t[["patient_id", "day", "worsening"]], on=["patient_id", "day"])
    a = a[a.day >= BASE]
    caught, leads = 0, []
    for r in ev.itertuples():
        win = a[(a.patient_id == r.patient_id) & (a.worsening == 1) & (a.alert == 1)]
        if len(win):
            caught += 1; leads.append(r.admission_day - win.day.min())
    normal = a[a.worsening == 0]
    spec = 1 - normal.alert.mean()
    # false-alert episodes: a run of alert days on normal days; a gap of >= 2 quiet days starts a new one
    eps = 0
    for pid, g in normal.groupby("patient_id"):
        on = g.sort_values("day")
        days = on[on.alert == 1].day.values
        if len(days):
            eps += 1 + int((np.diff(days) > 2).sum())
    months = len(normal) / 30.0
    return dict(sens=caught / len(ev), spec=spec, lead=np.median(leads) if leads else np.nan, fa=eps / months)

def weight_only(dd, thresh=2.0):
    out = []
    for pid, g in dd.groupby("patient_id"):
        g = g.sort_values("day"); w = g.weight_kg.values; al = np.zeros(len(g), int)
        for i in range(len(g)):
            prev = w[max(0, i - 7):i]; prev = prev[~np.isnan(prev)]
            if not np.isnan(w[i]) and len(prev) >= 3 and w[i] - np.median(prev) >= thresh:
                al[i] = 1
        out.append(pd.DataFrame({"patient_id": pid, "day": g.day.values, "alert": al}))
    return pd.concat(out)

def crude_multi(dd, thresh=4.0):
    """Fixed-baseline z-scores, no time-of-day correction, no adaptation: deliberately simple."""
    out = []
    for pid, g in dd.groupby("patient_id"):
        g = g.sort_values("day"); b = g[g.day < BASE]
        def z(col, sign, floor):
            med = b[col].median(); s = max(1.4826 * (b[col] - med).abs().median(), floor)
            return np.clip(sign * (g[col] - med) / s, 0, 4) if not np.isnan(med) else pd.Series(0.0, index=g.index)
        score = (z("weight_kg", 1, 0.3).fillna(0) + z("foot_z_ohm", -1, 0.02 * (b.foot_z_ohm.median() or 1)).fillna(0)
                 + z("rest_hr_bpm", 1, 3).fillna(0) + z("spo2_pct", -1, 0.8).fillna(0)
                 + z("resp_rate_bpm", 1, 1.5).fillna(0)
                 + 0.25 * np.maximum(z("ptt_ms", 1, 8).fillna(0), z("ptt_ms", -1, 8).fillna(0))   # secondary, direction unknown
                 + g[["q_orthopnea", "q_breathless", "q_swelling"]].sum(axis=1).fillna(0))
        high = (score >= thresh).values & g.weight_kg.notna().values
        meas = g.weight_kg.notna().values
        al = np.zeros(len(g), int); prev_high = False
        for i in range(len(g)):
            if meas[i]:
                al[i] = int(high[i] and prev_high); prev_high = high[i]
        out.append(pd.DataFrame({"patient_id": pid, "day": g.day.values, "alert": al}))
    return pd.concat(out)

res_w = evaluate(weight_only(d))
res_m = evaluate(crude_multi(d))
curve = []
for T in [2.5, 3, 3.5, 4, 5, 6, 7, 8]:
    r = evaluate(crude_multi(d, T)); r["T"] = T; curve.append(r)
curve = pd.DataFrame(curve)

lines += ["## C. Difficulty check (crude reference detectors, rules fixed in advance, all patients)", "",
          "These are not our detector. They check that the data is not trivially easy.", "",
          "| Reference detector | Events caught (sensitivity) | Normal days left alone (specificity) | Median lead time (days) | False alerts per patient per month |",
          "| --- | --- | --- | --- | --- |",
          f"| Weight only: +2 kg vs last 7 days | {res_w['sens']:.0%} | {res_w['spec']:.1%} | {res_w['lead']:.0f} | {res_w['fa']:.2f} |",
          f"| Crude multi-signal z-score, threshold 4, 2 days in a row | {res_m['sens']:.0%} | {res_m['spec']:.1%} | {res_m['lead']:.0f} | {res_m['fa']:.2f} |",
          "", "Crude multi-signal detector across thresholds:", "",
          "| Threshold | Sensitivity | Specificity | False alerts / patient / month |", "| --- | --- | --- | --- |"]
lines += [f"| {r.T} | {r.sens:.0%} | {r.spec:.1%} | {r.fa:.2f} |" for r in curve.itertuples()]
best_at_1fa = curve[curve.fa <= 1.0].sens.max() if (curve.fa <= 1.0).any() else 0
lines += ["", f"At about 1 false alert per patient per month or fewer, the crude detector catches at most "
          f"**{best_at_1fa:.0%}** of events.", ""]
realistic = best_at_1fa <= 0.85
lines += [("**Verdict: realistic difficulty.** A crude detector does not exceed the 76–88% range reported in the "
           "problem's best study at a usable false-alert rate, so there is room for a better detector without the data being trivial.")
          if realistic else
          "**Verdict: too easy.** A crude detector beats published results; effect sizes or noise need revising.", ""]

# ---------------- figures ----------------
fig, ax = plt.subplots(figsize=(6.5, 4))
ax.plot(curve.fa, curve.sens * 100, "-o", color=S1, lw=2, ms=6, label="Crude multi-signal")
ax.plot([res_w["fa"]], [res_w["sens"] * 100], "s", color=S2, ms=8, label="Weight only")
ax.axhspan(76, 88, color="#d9d8d3", alpha=0.5, lw=0)
ax.text(ax.get_xlim()[1] if False else curve.fa.max(), 89, "Best study in the problem: 76–88% caught", ha="right", va="bottom", color=INK2, fontsize=9)
ax.set_xlabel("False alerts per patient per month"); ax.set_ylabel("Events caught (%)"); ax.set_ylim(0, 100)
ax.set_title("Crude reference detectors on the synthetic data", loc="left", color=INK, fontsize=11)
ax.legend(frameon=False, loc="lower right"); plt.tight_layout(); plt.savefig("reports/difficulty_check.png", dpi=130); plt.close()

cols = [("weight_kg", "Weight (kg)"), ("foot_z_ohm", "Foot impedance (Ω)"), ("rest_hr_bpm", "Resting HR (bpm)"),
        ("spo2_pct", "SpO₂ (%)"), ("resp_rate_bpm", "Resp. rate (/min)"), ("ptt_ms", "PTT (ms)"), ("q_sum", "'Yes' answers"), ("sit_count", "Sit-downs/day")]
dt["q_sum"] = dt[["q_orthopnea", "q_breathless", "q_swelling"]].sum(axis=1, min_count=1)
picks = []
for ph in ["congestive", "no_weight_gain", "subtle"]:
    c = m[(m.phenotype == ph) & (m.has_implant == 0)]
    if len(c): picks.append(c.iloc[0])
fig, axes = plt.subplots(len(cols), len(picks), figsize=(4.2 * len(picks), 14), sharex="col")
for j, r in enumerate(picks):
    g = dt[dt.patient_id == r.patient_id]
    for i, (c, lab) in enumerate(cols):
        a = axes[i, j]
        a.plot(g.day, g[c], "-o", ms=2.5, lw=1, color=S1)
        w = g[g.worsening == 1].day
        a.axvspan(w.min() - .5, w.max() + .5, color=S2, alpha=0.15, lw=0)
        for dd in g[g.confounder.notna()].day: a.axvspan(dd - .5, dd + .5, color=S4, alpha=0.18, lw=0)
        if j == 0: a.set_ylabel(lab, fontsize=9)
    axes[0, j].set_title(f"{r.patient_id} · {r.phenotype.replace('_', ' ')} · worsening {int(r.lead_days)} d", loc="left", fontsize=10)
    axes[-1, j].set_xlabel("Day")
fig.suptitle("One patient per event type — orange: worsening window, yellow: confounder days", x=0.01, ha="left", fontsize=11)
plt.tight_layout(); plt.savefig("reports/example_patients.png", dpi=110); plt.close()

lines += ["## Figures", "", "- `example_patients.png`: one patient of each event type, with worsening and confounder days marked.",
          "- `difficulty_check.png`: the crude detectors against the published range.", "",
          "## How the data was built", "",
          "- 120 virtual patients, 60 days each (event patients stop the day before admission, always ≥ 30 days).",
          "- Each patient gets their own normal weight, foot impedance, heart rate, SpO₂, activity, usual test time, symptom-reporting habit and skip rate.",
          "- 60% have one admission. Worsening starts 3–10 days before it and accelerates towards admission.",
          "- Event types: congestive (fluid build-up, 60%), no weight gain (20%), subtle (20%, barely visible: some events are missed in real life too).",
          "- Real-life noise (kept simple in this version): weight ±0.3 kg, impedance ±2.5%, HR ±4 bpm, SpO₂ ±1 point, respiratory rate ±2.5 /min (rate estimated from 90 s of PPG is imprecise), PTT ±8 ms, slow drift in weight and HR.",
          "- Respiratory rate (from the MAX30102 PPG) rises 1.5–6 /min during worsening, less in subtle events; fever also raises it.",
          "- PTT is secondary: its change is small and its direction uncertain (−15 to +5 ms), matching the weak evidence; it gets one quarter weight in the reference detector.",
          "- Weight rises 0.07 kg per hour through the day; 15% of tests happen at a random hour.",
          "- Confounders: festival/salty-meal weekends, fever episodes, diuretic dose changes (a lasting baseline shift), heavy winter clothing.",
          "- Sensor failures: poor foot contact (5%), finger moved (6% SpO₂ missing), both heart-rate sensors failed (2%).",
          "- Pacemaker/ICD patients (25%) have no impedance readings.", "",
          "**Limit:** the generator encodes our assumptions about effect sizes; validation on it shows the method works on these patterns, not on real patients."]
open("reports/validation_report.md", "w").write("\n".join(lines))
print("\n".join(lines[:60]))
