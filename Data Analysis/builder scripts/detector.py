"""
SAATHI chair - early-warning detector (Checkpoint 3).

Input: what the chair reports each day (data/measured.csv, produced from raw signals in Checkpoint 2).
Output per patient-day: score, alert yes/no, and the reason in plain words.

1. Personal baseline: median and spread of each signal over the first 14 days; afterwards it keeps
   updating from days at least 11 days old whose score stayed low, so a slow change in normal
   (e.g. a new diuretic dose) is learned but a 3-10 day worsening is not absorbed.
2. Daily points: how far each signal is from the patient's normal, in the direction that means
   worsening; only the part beyond 1 spread counts. Weights are fixed in advance by evidence strength.
   Weight is corrected for time of day before comparison.
3. Alert: score above threshold on 2 measured days in a row, or a danger rule
   (weight +2 kg within 3 days, or SpO2 below 90% on 2 measured days in a row).
4. Reason: the signals that contributed most, in natural units.
"""
import numpy as np
import pandas as pd

BASELINE_DAYS = 14
BASE_LAG = 11            # days a reading must age before it can update the baseline
SIGNALS = {
    #  column           direction  weight  minimum spread   kind of spread   label
    "weight_adj":     (+1, 1.00, 0.40, "abs", "Weight"),
    "foot_z_ohm":     (-1, 1.00, 0.02, "rel", "Foot fluid"),
    "rest_hr_bpm":    (+1, 0.70, 3.0,  "abs", "Resting heart rate"),
    "resp_rate_bpm":  (+1, 0.70, 2.0,  "abs", "Breathing rate"),
    "spo2_pct":       (-1, 0.50, 1.0,  "abs", "Oxygen (SpO2)"),
    "ptt_ms":         (0,  0.25, 8.0,  "abs", "Pulse transit time"),     # secondary; direction unknown -> both ways
    "sit_count":      (-1, 0.25, 0.20, "rel", "Activity (sit-downs)"),   # optional activity signal
}
Q_WEIGHT = 0.70          # points per 'yes' answer
QUESTIONS = {"q_orthopnea": "reported needing extra pillows to breathe", "q_breathless": "reported breathlessness",
             "q_swelling": "reported more foot swelling"}


def fit_time_of_day_slope(m, train_ids, truth=None):
    """kg per hour of test time, learned from training patients' normal days only."""
    g = m[m.patient_id.isin(train_ids) & m.weight_kg.notna()].copy()
    if truth is not None:
        g = g.merge(truth[["patient_id", "day", "worsening"]], on=["patient_id", "day"])
        g = g[g.worsening == 0]
    g["w"] = g.weight_kg - g.groupby("patient_id").weight_kg.transform("mean")
    g["h"] = g.test_hour - g.groupby("patient_id").test_hour.transform("mean")
    return float((g.w * g.h).sum() / (g.h ** 2).sum())


def _spread(vals, floor, kind, med):
    s = 1.4826 * np.nanmedian(np.abs(vals - med)) if len(vals) else np.nan
    f = floor * abs(med) if kind == "rel" else floor
    return max(s if not np.isnan(s) else f, f)


BASE_EXCLUDE = 2.0       # days scoring at least this never update the baseline (fixed, not the alert threshold)


def score_patient(g, slope, weights=None):
    """Score every day of one patient. Independent of the alert threshold."""
    w_over = weights or {}
    g = g.sort_values("day").reset_index(drop=True)
    ref_hour = g.loc[g.day < BASELINE_DAYS, "test_hour"].median()
    g["weight_adj"] = g.weight_kg - slope * (g.test_hour - ref_hour)
    days = g.day.values
    excluded = np.zeros(len(g), bool)
    rows = []
    for i, r in g.iterrows():
        d = r.day
        res = dict(patient_id=r.patient_id, day=d, measured=int(r.missed == 0), score=np.nan,
                   danger="", reason="", in_baseline=int(d < BASELINE_DAYS))
        if d < BASELINE_DAYS or r.missed == 1:
            rows.append(res); continue
        pool_mask = (days < BASELINE_DAYS) | ((days <= d - BASE_LAG) & ~excluded)
        pool = g[pool_mask]
        contrib, words = {}, {}
        for col, (direc, wt, floor, kind, label) in SIGNALS.items():
            wt = w_over.get(col, wt)
            v = r[col]
            if wt == 0 or np.isnan(v):
                continue
            base_vals = pool[col].dropna().values[-28:]
            if len(base_vals) < 5:
                continue
            med = np.median(base_vals)
            s = _spread(base_vals, floor, kind, med)
            z = (v - med) / s
            z = abs(z) if direc == 0 else direc * z
            pts = float(np.clip(z - 1.0, 0, 3)) * wt       # only the part beyond 1 spread counts
            if pts > 0:
                contrib[col] = pts
                delta = v - med
                if col == "foot_z_ohm":
                    words[col] = f"Foot fluid up (impedance {100 * delta / med:+.0f}%)"
                elif col == "sit_count":
                    words[col] = f"{label} {100 * delta / med:+.0f}%"
                elif col == "weight_adj":
                    words[col] = f"{label} {delta:+.1f} kg"
                elif col == "spo2_pct":
                    words[col] = f"{label} {delta:+.1f} points"
                elif col == "ptt_ms":
                    words[col] = f"{label} {delta:+.0f} ms"
                else:
                    words[col] = f"{label} {delta:+.0f} {'bpm' if col == 'rest_hr_bpm' else '/min'}"
        for q, txt in QUESTIONS.items():
            if r[q] == 1 and w_over.get(q, Q_WEIGHT) > 0:
                contrib[q] = w_over.get(q, Q_WEIGHT); words[q] = txt
        score = sum(contrib.values())
        excluded[i] = score >= BASE_EXCLUDE
        danger = ""
        recent = g[(g.day >= d - 3) & (g.day < d)].weight_adj.dropna()
        if w_over.get("danger_weight", 1) and len(recent) and r.weight_adj - recent.min() >= 2.0:
            danger = "weight"
        top = sorted(contrib, key=contrib.get, reverse=True)
        res.update(score=round(score, 2), danger=danger, reason="; ".join(words[k] for k in top[:4]),
                   spo2_low=int((not np.isnan(r.spo2_pct)) and r.spo2_pct < 90))
        rows.append(res)
    out = pd.DataFrame(rows)
    if "spo2_low" not in out:
        out["spo2_low"] = 0
    out["spo2_low"] = out["spo2_low"].fillna(0).astype(int)
    return out


def apply_threshold(scores, threshold, use_score=True, use_danger=True):
    """Alert = score high on 2 measured days in a row, or a danger rule."""
    out = []
    for pid, g in scores.groupby("patient_id", sort=False):
        g = g.sort_values("day").copy()
        alert, rule = [], []
        high_prev = sp_prev = False
        for r in g.itertuples():
            if np.isnan(r.score):
                alert.append(0); rule.append(""); continue
            high = r.score >= threshold
            ru = ""
            if use_score and high and high_prev:
                ru = "score high 2 days in a row"
            if use_danger and not ru and r.danger == "weight":
                ru = "danger: weight +2 kg in 3 days"
            if use_danger and not ru and r.spo2_low and sp_prev:
                ru = "danger: SpO2 below 90% two days running"
            high_prev, sp_prev = high, bool(r.spo2_low)
            alert.append(int(bool(ru))); rule.append(ru)
        g["alert"], g["rule"] = alert, rule
        out.append(g)
    return pd.concat(out, ignore_index=True)


def score_all(m, slope, ids=None, weights=None):
    ids = ids if ids is not None else m.patient_id.unique()
    return pd.concat([score_patient(m[m.patient_id == p], slope, weights) for p in ids], ignore_index=True)


def run_all(m, slope, threshold, ids=None, weights=None):
    return apply_threshold(score_all(m, slope, ids, weights), threshold)


def evaluate(alerts, truth, patients):
    """Metrics exactly as the problem defines them, on days after the 14-day baseline."""
    a = alerts.merge(truth[["patient_id", "day", "worsening"]], on=["patient_id", "day"])
    a = a[a.day >= BASELINE_DAYS]
    pts = patients[patients.patient_id.isin(a.patient_id.unique())]
    ev = pts[pts.has_event == 1]
    caught, leads, per = 0, [], []
    for r in ev.itertuples():
        win = a[(a.patient_id == r.patient_id) & (a.worsening == 1) & (a.alert == 1)]
        hit = len(win) > 0
        caught += hit
        if hit:
            leads.append(r.admission_day - win.day.min())
        per.append(dict(patient_id=r.patient_id, phenotype=r.phenotype, caught=int(hit),
                        lead=(r.admission_day - win.day.min()) if hit else np.nan))
    normal = a[a.worsening == 0]
    eps = 0
    for _, g in normal.groupby("patient_id"):
        days = g[g.alert == 1].day.sort_values().values
        if len(days):
            eps += 1 + int((np.diff(days) > 2).sum())
    return dict(events=len(ev), caught=caught, sens=caught / len(ev) if len(ev) else np.nan,
                spec=1 - normal.alert.mean(), lead=float(np.median(leads)) if leads else np.nan,
                fa=eps / (len(normal) / 30.0), fa_episodes=eps, normal_days=len(normal),
                per_event=pd.DataFrame(per))


def wilson(k, n, z=1.96):
    if n == 0:
        return (np.nan, np.nan)
    p = k / n
    den = 1 + z * z / n
    c = (p + z * z / (2 * n)) / den
    h = z * np.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / den
    return (c - h, c + h)
