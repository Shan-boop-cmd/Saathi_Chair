# SAATHI: references

Every source below contributed something specific to SAATHI. Each entry says **what we took from it** and **where it is used** (files in this repository, plus the report, deck and video where relevant).

**How we used them, honestly:**
- For the four heart-failure studies (A1–A4), we used the abstracts as summarised in the problem statement, not the full papers.
- The signal-processing and statistics sources (B, C) are the standard references for methods we implemented from
  general knowledge of the field. They are cited for the method, not as data.
- No result in this repository is clinical evidence. All patients are computer-generated; real recordings were used
  only to check beat detection.

**DOI check:**
- [A4]: confirmed on the MD Anderson repository record.
- [B1], [B2], [B4], [B6], [B7]: a web search on the DOI returned the paper's record or full text.
- The other DOIs and citations match the publisher records known to us, but the publisher sites could not be opened
  from our build machine. Please confirm them on the publisher site before quoting page numbers.

---

## A. Heart-failure evidence (why these signals, and how worsening is simulated)

**[A1] Chaudhry SI, Wang Y, Concato J, Gill TM, Krumholz HM.** Patterns of weight change preceding hospitalization for
heart failure. *Circulation.* 2007;116(14):1549–1554. doi:10.1161/CIRCULATIONAHA.107.690768
- *What we took:* weight gain begins about a week or more before many heart-failure admissions, but not all of them.
  So weight is a core signal, but it can't be the only one.
- *Where used:*
  - `generate_data.py`: worsening trajectories include weight gain for "congestive" events, and a "no weight gain"
    event type for admissions without gain.
  - `detector.py` and `firmware/saathi_chair/detector.cpp`: weight has the highest signal weight (1.0), plus the
    "+2 kg in 3 days" danger rule.
  - `run_detector.py`: the weight-only comparison rule, which catches 23%.
  - `report/report.md` (Evidence, [1]); deck "problem" slide; explainer video scene 2.

**[A2] Yu CM, Wang L, Chau E, et al.** Intrathoracic impedance monitoring in patients with heart failure: correlation
with fluid status and feasibility of early warning preceding hospitalization. *Circulation.* 2005;112(6):841–848.
doi:10.1161/CIRCULATIONAHA.104.492207
- *What we took:* impedance measured by an implanted device falls in the days to weeks before admission, as fluid
  builds up. SAATHI's **foot** impedance is our own untested adaptation of this idea and is kept as exploratory.
- *Where used:*
  - `generate_data.py`: foot impedance falls during worsening.
  - `hardware/bioimpedance_sim.py` and `hardware/schematic_bioimpedance.*`: the 50 kHz foot-plate circuit.
  - `detector.py`: foot impedance is weighted 1.0, and only a drop scores.
  - `report/report.md` ([2]); deck "problem" slide; video scene 2.

**[A3] Inan OT, Baran Pouyan M, Javaid AQ, et al.** Novel wearable seismocardiography and machine learning algorithms
can assess clinical status of heart failure patients. *Circulation: Heart Failure.* 2018;11(1):e004313.
doi:10.1161/CIRCHEARTFAILURE.117.004313
- *What we took:* cardiac vibration measured with a chest patch tracked heart-failure status. SAATHI's seat
  **ballistocardiography** (BCG) is a related but different measurement, and we use it only for heart rate.
- *Where used:*
  - The choice of a seat PVDF film: `hardware/bcg_frontend_sim.py`, `hardware/schematic_bcg_frontend.*`,
    `sim_signals.py`, `pipeline.py` (process_bcg) and `firmware/saathi_chair/dsp.cpp`.
  - `report/report.md` ([3]).

**[A4] Stehlik J, Schmalfuss C, Bozkurt B, et al.** Continuous wearable monitoring analytics predict heart failure
hospitalization: the LINK-HF multicenter study. *Circulation: Heart Failure.* 2020;13(3):e006513.
doi:10.1161/CIRCHEARTFAILURE.119.006513
- *What we took:*
  - **Several signals combined against each patient's own baseline** beat any single signal. This is SAATHI's core
    design.
  - The benchmark of 76–88% of events caught, about 6.5 days early, with a chest patch.
- *Where used:*
  - `detector.py` and `firmware/saathi_chair/detector.cpp`: personal baseline plus a multi-signal score.
  - The benchmark is quoted in `report/report.md` ([4]), the deck "problem" and "results" slides, and
    `JUDGE_QA.md`.

## B. Signal-processing methods we implemented

**[B1] Inan OT, Migeotte PF, Park KS, et al.** Ballistocardiography and seismocardiography: a review of recent
advances. *IEEE J Biomed Health Inform.* 2015;19(4):1414–1427. doi:10.1109/JBHI.2014.2361732
- *What we took:*
  - The BCG waveform, with the J-wave as the dominant peak of each heartbeat.
  - Its frequency content, which is why we band-pass at 1–10 Hz, and its sensitivity to movement.
- *Where used:*
  - `sim_signals.py` (simulate_bcg).
  - `pipeline.py` (process_bcg: band-pass, J-wave peaks, median beat interval).
  - `firmware/saathi_chair/dsp.cpp`.
  - `hardware/bcg_frontend_sim.py`: the filter pass band.

**[B2] Chan ED, Chan MM, Chan MM.** Pulse oximetry: understanding its basic principles facilitates appreciation of its
limitations. *Respir Med.* 2013;107(6):789–799. doi:10.1016/j.rmed.2013.02.004
- *What we took:* the red/infrared "ratio of ratios" principle behind SpO₂, and its limits: motion, low perfusion and
  the need for calibration. This is why SpO₂ is used only as a trend, and why the chair rejects readings when
  beat-to-beat ratios disagree.
- *Where used:*
  - `pipeline.py` (process_ppg: per-beat AC/DC ratio, spread gate).
  - `sim_signals.py` (simulate_ppg).
  - `firmware/saathi_chair/dsp.cpp`.
  - `detector.py`: SpO₂ is weighted only 0.5, and the danger rule is relative to the person's own normal.
- *Note:* the linear calibration SpO₂ ≈ 110 − 25·R is an engineering approximation used in common MAX3010x example
  code, not a peer-reviewed curve. A real sensor needs calibration (stated in `pipeline.py`).

**[B3] Karlen W, Raman S, Ansermino JM, Dumont GA.** Multiparameter respiratory rate estimation from the
photoplethysmogram. *IEEE Trans Biomed Eng.* 2013;60(7):1946–1953. doi:10.1109/TBME.2013.2246160
- *What we took:* breathing modulates the pulse wave in three ways: baseline (RIIV), amplitude (RIAV) and beat
  interval (RIFV). We estimate the rate from each and fuse the three.
- *Where used:*
  - `pipeline.py` (respiratory-rate step of process_ppg: three modulations, median of three).
  - `sim_signals.py`: the simulated PPG carries all three modulations.
  - `firmware/saathi_chair/dsp.cpp` ("median of baseline, amplitude and interval modulation estimates").

**[B4] Charlton PH, Bonnici T, Tarassenko L, et al.** An assessment of algorithms to estimate respiratory rate from the
electrocardiogram and photoplethysmogram. *Physiol Meas.* 2016;37(4):610–626. doi:10.1088/0967-3334/37/4/610
- *What we took:*
  - Fusing several modulation-based estimates is more reliable than any one of them.
  - PPG breathing rate is often inaccurate, so we added an agreement gate (no reading unless the estimates agree
    within 2.5 breaths/min) and report the signal as weak.
- *Where used:*
  - `pipeline.py` (RR_AGREE_BPM gate) and `firmware/saathi_chair/config.h` (`RR_AGREE_BPM`).
  - `reports/real_signal_check.md` and `report/report.md` (Limits).

**[B5] Pan J, Tompkins WJ.** A real-time QRS detection algorithm. *IEEE Trans Biomed Eng.* 1985;32(3):230–236.
doi:10.1109/TBME.1985.325532
- *What we took:* the classic QRS-detection pipeline: band-pass, squaring to emphasise the QRS energy, then adaptive
  thresholding. Our ECG checker is a simplified version (5–20 Hz band, squared signal, adaptive peaks, median
  interval).
- *Where used:*
  - `real_signal_check.py` and `bidmc_hr_check.py`: checking the chair's beat detection against expert ECG labels,
    which agreed within 0.03 bpm.

**[B6] Mukkamala R, Hahn JO, Inan OT, et al.** Toward ubiquitous blood pressure monitoring via pulse transit time:
theory and practice. *IEEE Trans Biomed Eng.* 2015;62(8):1879–1901. doi:10.1109/TBME.2015.2441951
- *What we took:*
  - The definition of pulse transit time (PTT), and the intersecting-tangent method for finding the pulse foot.
  - The caution that PTT depends on many factors. So PTT is a secondary signal with weight 0.25, scored in either
    direction.
- *Where used:*
  - `pipeline.py` (process_ptt and the intersecting-tangent foot in process_ppg).
  - `firmware/saathi_chair/dsp.cpp` (intersecting-tangent foot; "J-wave → next pulse foot").
  - `sim_signals.py`: each finger pulse arrives PTT ms after the BCG J-wave.
  - `detector.py` (PTT weight 0.25).

**[B7] Kyle UG, Bosaeus I, De Lorenzo AD, et al.** Bioelectrical impedance analysis—part I: review of principles and
methods. *Clin Nutr.* 2004;23(5):1226–1243. doi:10.1016/j.clnu.2004.06.004
- *What we took:*
  - The principles of single-frequency (50 kHz) bioimpedance and the four-electrode (tetrapolar) method, which
    removes skin-contact impedance.
  - Impedance falls as body water rises.
- *Where used:*
  - `hardware/bioimpedance_sim.py` (50 kHz, 4 plates, ratio against a 470 Ω reference; contact-impedance test).
  - `hardware/schematic_bioimpedance.*`.
  - `generate_data.py` (foot_z falls with fluid).
  - `firmware/saathi_chair/sensors.cpp` (impedance reading).

## C. Statistics

**[C1] Leys C, Ley C, Klein O, Bernard P, Licata L.** Detecting outliers: do not use standard deviation around the
mean, use absolute deviation around the median. *J Exp Soc Psychol.* 2013;49(4):764–766.
doi:10.1016/j.jesp.2013.03.013
- *What we took:* median ± MAD (scaled by 1.4826) as a robust "normal range" that one festival meal or bad reading
  can't distort.
- *Where used:*
  - `detector.py`: the personal baseline, `1.4826 * median(|x − median|)`.
  - `firmware/saathi_chair/detector.cpp` (madOf / medianOf).
  - `JUDGE_QA.md` ("Why median and MAD?").

**[C2] Wilson EB.** Probable inference, the law of succession, and statistical inference. *J Am Stat Assoc.*
1927;22(158):209–212. doi:10.1080/01621459.1927.10502953
- *What we took:* the Wilson score interval, used for 95% confidence intervals on sensitivity with small event counts.
- *Where used:*
  - `locked_eval.py`, `run_detector.py`, `detector.py` (the 80%, 95% CI 72–86% figure, and the subgroup intervals).

## D. Real data used to test the signal processing

**[D1] Goldberger AL, Amaral LAN, Glass L, et al.** PhysioBank, PhysioToolkit, and PhysioNet: components of a new
research resource for complex physiologic signals. *Circulation.* 2000;101(23):e215–e220.
doi:10.1161/01.CIR.101.23.e215
- *Where used:* every real recording in `real_signal_check.py` and `bidmc_hr_check.py` comes from PhysioNet.
  Results are in `reports/real_signal_check.md`.

**[D2] Baim DS, Colucci WS, Monrad ES, et al.** Survival of patients with severe congestive heart failure treated with
oral milrinone. *J Am Coll Cardiol.* 1986;7(3):661–670.
- *What we took:* this is the source of the BIDMC Congestive Heart Failure Database named in the problem statement.
- *Where used:* `bidmc_hr_check.py`, which runs the chair's beat detector on it. It runs on a laptop; PhysioNet was
  blocked on our build machine.

**[D3] Moody GB, Mark RG.** The impact of the MIT-BIH Arrhythmia Database. *IEEE Eng Med Biol Mag.* 2001;20(3):45–50.
doi:10.1109/51.932724
- *Where used:* record 100 in `real_signal_check.py`: beat detection against expert labels.

**[D4] Clifford GD, Silva I, Moody B, et al.** The PhysioNet/Computing in Cardiology Challenge 2015: reducing false
arrhythmia alarms in the ICU. *Computing in Cardiology.* 2015;42:273–276.
- *Where used:* records a103l and v102s in `real_signal_check.py`: pulse-wave heart rate and breathing on real PPG,
  plus a corrupted recording that was correctly rejected.

**[D5] WFDB Software Package for Python** (MIT Laboratory for Computational Physiology), version 4.3.1, and its sample
data. https://github.com/MIT-LCP/wfdb-python
- *Where used:* reading every PhysioNet record in `real_signal_check.py` and `bidmc_hr_check.py`.

## E. Safety standard

**[E1] IEC 60601-1:2005+A1:2012.** Medical electrical equipment, Part 1: General requirements for basic safety and
essential performance.
- *What we took:* the type-BF patient auxiliary current limits: 10 µA DC and 100 µA AC in normal condition, and
  50 µA DC and 500 µA AC in single-fault condition.
- *Where used:*
  - `hardware/fault_sim.py`: every part failed one at a time; worst case ≤ 1.65 µA DC and ≤ 51 µA rms.
  - The battery-only and no-pacemaker interlocks in `firmware/saathi_chair/`.
  - `report/report.md` (Hardware) and `JUDGE_QA.md`.
- A public summary of the limits we checked against:
  https://www.advancedenergy.com/en-us/about/news/blog/safety-requirements-in-medical-equipment-designing-for-bf-and-cf-classifications/

## F. Component datasheets (engineering inputs, not scientific evidence)

Manufacturer datasheets for the **HX711** (Avia Semiconductor) load-cell ADC, the **MAX30102** (Maxim Integrated)
pulse-oximetry sensor, the **ESP32-WROOM-32** (Espressif), the **SIM800L** (SIMCom) GSM module, the **MCP6024**
(Microchip) op-amp and the **CD4066** analogue switch.
- *Where used:*
  - Sample rates and resolution in `sim_signals.py`.
  - Current draw in `hardware/power_budget.py` (about 20 days on battery).
  - The op-amp class (10 MHz gain-bandwidth, rail-to-rail) in `hardware/bioimpedance_sim.py`.
  - Pin and timing settings in `firmware/saathi_chair/config.h`.
- Prices are not from datasheets; they come from the Indian vendor pages linked in `hardware/bom.csv`.

## G. Software tools

- **ngspice** circuit simulator (https://ngspice.sourceforge.io/): every circuit simulation in `hardware/`.
- **Arduino-ESP32 core 3.0.7** and **Espressif QEMU**: firmware build and the emulator run in
  `firmware/qemu_demo_log.txt`.
- **HX711 library** by Bogdan Necula and the **SparkFun MAX3010x** library: sensor drivers in
  `firmware/saathi_chair/sensors.cpp`.
- **NumPy, SciPy, pandas, Matplotlib**: data generation, signal processing, detector and figures.
- **Kokoro-82M** open-source text-to-speech (`kokoro-onnx`): the explainer video's voiceover (`video/build/`).
- **Playwright** (Chromium) and **FFmpeg**: rendering the dashboard screenshots, the report PDF and the explainer video.
