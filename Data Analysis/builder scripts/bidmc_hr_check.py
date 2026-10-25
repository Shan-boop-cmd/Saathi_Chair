"""
SAATHI - heart-rate check on real heart-failure patients (BIDMC Congestive Heart Failure Database, PhysioNet chfdb).

Runs the SAME heart-rate method as the chair pipeline (band-pass, adaptive peak detection, median beat
interval -> bpm, from pipeline.py) on real ECGs from 15 heart-failure patients, in 90-second windows like
the chair's daily check. Reference: the database's expert beat annotations.

Run on your laptop (needs internet to PhysioNet):
    pip install wfdb scipy numpy pandas
    python bidmc_hr_check.py                 # all 15 records, 40 windows each
    python bidmc_hr_check.py --records chf01 chf02 --windows 20
    python bidmc_hr_check.py --selftest      # offline: synthetic ECG, checks the code runs
Writes reports/bidmc_hr_results.csv and reports/bidmc_hr_report.md
"""
import argparse, os
import numpy as np
import pandas as pd
from pipeline import bandpass, detect_beats, hr_from_peaks

WIN = 90.0
os.makedirs("reports", exist_ok=True)


def hr_from_ecg(sig, fs):
    """Chair heart-rate method applied to ECG: QRS band 5-20 Hz, squared energy, adaptive peaks, median interval."""
    y = bandpass(sig, fs, 5.0, 20.0) ** 2
    pk = detect_beats(y, fs, min_gap_s=0.30)
    return hr_from_peaks(pk / fs)


def reference_hr(ann_samples, fs, a, b):
    s = ann_samples[(ann_samples >= a) & (ann_samples < b)] / fs
    return 60.0 / np.median(np.diff(s)) if len(s) > 10 else np.nan


def run(records, n_windows, rng):
    import wfdb
    rows = []
    for rec in records:
        h = wfdb.rdheader(rec, pn_dir="chfdb")
        fs, n = h.fs, h.sig_len
        ann = wfdb.rdann(rec, "ecg", pn_dir="chfdb")
        beats = ann.sample[np.isin(ann.symbol, ["N", "V", "S", "A", "F", "Q", "/", "f", "L", "R", "j", "e", "a", "J"])]
        starts = rng.integers(0, int(n - WIN * fs), n_windows)
        for st in starts:
            r = wfdb.rdrecord(rec, pn_dir="chfdb", sampfrom=int(st), sampto=int(st + WIN * fs), channels=[0])
            est, q = hr_from_ecg(r.p_signal[:, 0], fs)
            ref = reference_hr(beats, fs, st, st + WIN * fs)
            rows.append(dict(record=rec, start_s=round(st / fs), est_bpm=est, ref_bpm=ref, quality=q))
        print(rec, "done")
    return pd.DataFrame(rows)


def selftest(rng):
    """Offline check with a synthetic ECG (spiky QRS + noise), so the script can be verified without PhysioNet."""
    fs, rows = 250, []
    for i in range(40):
        hr = rng.uniform(50, 110)
        t = np.arange(0, WIN, 1 / fs)
        bt = np.cumsum(60 / hr * (1 + 0.03 * rng.standard_normal(int(WIN * hr / 60) + 5)))
        x = 0.05 * rng.standard_normal(len(t)) + 0.3 * np.sin(2 * np.pi * 0.25 * t)
        for b in bt[bt < WIN]:
            x += np.exp(-0.5 * ((t - b) / 0.012) ** 2) - 0.2 * np.exp(-0.5 * ((t - b - 0.03) / 0.015) ** 2)
        est, q = hr_from_ecg(x, fs)
        ref = 60 / np.median(np.diff(bt[bt < WIN]))
        rows.append(dict(record="synthetic", start_s=0, est_bpm=est, ref_bpm=ref, quality=q))
    return pd.DataFrame(rows)


def report(df, title):
    df = df.dropna(subset=["ref_bpm"])
    e = df.est_bpm - df.ref_bpm
    ok = e.abs() <= 5
    L = [f"# {title}", "", f"{len(df)} windows of 90 s from {df.record.nunique()} record(s).", "",
         "| Measure | Value |", "| --- | --- |",
         f"| Mean absolute error | {e.abs().mean():.2f} bpm |",
         f"| Bias | {e.mean():+.2f} bpm |",
         f"| Windows within ±5 bpm | {ok.mean():.0%} |",
         f"| Windows with no estimate | {df.est_bpm.isna().sum()} |", "",
         "Per record:", "", "| Record | Windows | Mean abs. error (bpm) | Within ±5 bpm |", "| --- | --- | --- | --- |"]
    for rec, g in df.groupby("record"):
        ee = g.est_bpm - g.ref_bpm
        L.append(f"| {rec} | {len(g)} | {ee.abs().mean():.2f} | {(ee.abs() <= 5).mean():.0%} |")
    L += ["", "Heart-failure ECGs include extra beats and irregular rhythms; the median beat interval keeps the "
          "estimate stable, but irregular windows are where errors concentrate."]
    return "\n".join(L)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--records", nargs="*", default=[f"chf{i:02d}" for i in range(1, 16)])
    ap.add_argument("--windows", type=int, default=40)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    rng = np.random.default_rng(15)
    if a.selftest:
        df = selftest(rng); title = "Heart-rate method self-test (synthetic ECG, offline)"
        out = "reports/bidmc_selftest"
    else:
        df = run(a.records, a.windows, rng); title = "Heart-rate check on the BIDMC CHF database"
        out = "reports/bidmc_hr"
    df.to_csv(out + "_results.csv", index=False)
    txt = report(df, title)
    open(out + "_report.md", "w").write(txt)
    print(txt)
