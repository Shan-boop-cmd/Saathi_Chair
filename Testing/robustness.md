# Robustness of the detector

Every run repeats the full protocol: new synthetic patients, patient-level split, time-of-day correction and alert threshold learned on training patients only, one scoring of the held-out patients. Values are mean (min–max) over runs. "Simplest" = the same detector without foot impedance, PTT and chair use. False alerts are episodes per 30 normal patient-days (after the learning period, outside worsening windows).

**E0, recorded values vs values recovered from simulated waveforms (main dataset, headline split):** sensitivity 77% vs 74%, false alerts 0.84 vs 0.83 per 30 normal patient-days. The fast path below is a fair stand-in.

## E1 and E2: is the headline result luck?

| Experiment | Runs | Sensitivity | Specificity | False alerts / 30 normal days | Warned ≥ 3 days ahead | Thresholds chosen |
| --- | --- | --- | --- | --- | --- | --- |
| E1: 10 new datasets (seeds 101–110) | 10 | 77% (71%–84%) | 96% (94%–97%) | 0.74 (0.59–1.00) | 42% (33%–60%) | 1.25: 10× |
| E2: 20 random patient splits of the main dataset | 20 | 77% (69%–85%) | 96% (96%–98%) | 0.75 (0.42–0.87) | 49% (33%–64%) | 1.25: 19×, 1.5: 1× |

Simplest detector on the same 10 datasets: sensitivity 75% (68%–79%), false alerts 0.76 (0.47–0.97).

## E3: situations the detector was not designed on (5 datasets each)

| Scenario | Sensitivity | Specificity | False alerts / 30 normal days | Warned ≥ 3 days ahead | Simplest: sensitivity | Simplest: false alerts |
| --- | --- | --- | --- | --- | --- | --- |
| Default settings (E1) | 77% (71%–84%) | 96% (94%–97%) | 0.74 (0.59–1.00) | 42% (33%–60%) | 75% (68%–79%) | 0.76 (0.47–0.97) |
| Slow worsening, 14-30 days | 96% (95%–97%) | 94% (93%–95%) | 1.03 (0.88–1.23) | 93% (91%–95%) | 96% (94%–98%) | 0.81 (0.66–0.97) |
| Sudden (step) onset | 94% (87%–97%) | 94% (92%–96%) | 0.98 (0.75–1.21) | 87% (79%–97%) | 92% (87%–95%) | 0.88 (0.69–0.96) |
| Late surge | 65% (57%–75%) | 95% (93%–96%) | 0.87 (0.65–1.32) | 27% (22%–30%) | 67% (54%–78%) | 0.86 (0.70–0.95) |
| Linear ramp | 86% (78%–94%) | 95% (94%–96%) | 0.93 (0.74–1.09) | 64% (54%–77%) | 86% (76%–94%) | 0.84 (0.61–1.22) |
| Skip checks when unwell | 65% (59%–72%) | 96% (95%–96%) | 0.76 (0.62–0.93) | 40% (26%–53%) | 67% (62%–75%) | 0.79 (0.69–0.91) |
| Slow sensor drift | 80% (74%–95%) | 95% (94%–97%) | 0.81 (0.59–1.11) | 49% (44%–57%) | 83% (78%–95%) | 0.83 (0.51–1.08) |
| Weaker effects (x0.6) | 74% (66%–83%) | 95% (94%–97%) | 0.86 (0.73–0.97) | 46% (32%–53%) | 70% (62%–74%) | 0.83 (0.68–1.08) |
| More look-alike illness (x2.5) | 71% (61%–81%) | 95% (93%–97%) | 0.73 (0.52–0.92) | 38% (29%–53%) | 73% (63%–82%) | 0.84 (0.68–1.00) |
| More noise (x1.5) | 70% (61%–77%) | 95% (94%–96%) | 0.93 (0.84–1.05) | 39% (32%–55%) | 71% (61%–79%) | 0.92 (0.77–1.15) |
| All hard at once | 53% (45%–61%) | 96% (93%–97%) | 0.80 (0.61–1.21) | 28% (15%–34%) | 54% (50%–63%) | 0.84 (0.67–1.12) |
