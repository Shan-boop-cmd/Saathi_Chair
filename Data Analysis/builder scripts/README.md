# SAATHI --- Chair-Based Heart-Failure Monitoring Research Prototype

SAATHI is a research prototype exploring whether a chair equipped with
multiple sensors can collect daily physiological measurements and
identify patterns that may warrant earlier clinical review in older
adults at risk of acute decompensated heart failure (ADHF).

The software is organized into four Python scripts:

1.  **`generate_data.py`** --- generates synthetic patient timelines,
    disease trajectories, and everyday confounders.
2.  **`pipeline.py`** --- processes raw sensor waveforms into
    quality-controlled daily measurements.
3.  **`detector.py`** --- compares measurements with a patient's
    personal baseline and generates explainable warning flags.
4.  **`bidmc_hr_check.py`** --- evaluates the heart-rate extraction
    method against annotated ECG recordings from the PhysioNet BIDMC
    Congestive Heart Failure Database, with an offline synthetic
    self-test.

> **Important clinical limitation:** SAATHI is a research and
> engineering prototype, not a medical device or a diagnostic system. An
> alert is an observational risk signal, not a diagnosis. The synthetic
> cohort is simulated data, not evidence of clinical performance.
> Clinical use would require appropriate device validation, prospective
> studies, clinical oversight, privacy safeguards, and regulatory
> review.

------------------------------------------------------------------------

## Contents

-   [System overview](#system-overview)
-   [Repository workflow](#repository-workflow)
-   [Script 1: `generate_data.py`](#script-1-generate_datapy)
-   [Script 2: `pipeline.py`](#script-2-pipelinepy)
-   [Script 3: `detector.py`](#script-3-detectorpy)
-   [Script 4: `bidmc_hr_check.py`](#script-4-bidmc_hr_checkpy)
-   [How the scoring weights work](#how-the-scoring-weights-work)
-   [Expected data files](#expected-data-files)
-   [Installation](#installation)
-   [Running the scripts](#running-the-scripts)
-   [Interpreting the results](#interpreting-the-results)
-   [Engineering assumptions and
    limitations](#engineering-assumptions-and-limitations)
-   [Suggested development sequence](#suggested-development-sequence)

------------------------------------------------------------------------

## System overview

The scripts cover three development checkpoints and one independent
validation task.

``` mermaid
flowchart TD
    A[generate_data.py<br/>Synthetic cohort and ground truth] --> B[data/daily.csv<br/>Measurements visible to detector]
    A --> C[data/truth.csv<br/>Hidden event labels and confounders]
    A --> D[data/patients.csv<br/>Patient metadata]

    E[Raw chair sensor waveforms<br/>90-second session] --> F[pipeline.py<br/>Filtering and quality checks]
    F --> G[Daily measurement row<br/>weight, impedance, HR, SpO2, RR, PTT]

    B --> H[detector.py]
    G --> H
    H --> I[Personal baseline and risk score]
    I --> J[Warning flags and plain-language reasons]
    C -. Evaluation only; not a detector input .-> K[Evaluation metrics]
    J --> K

    L[PhysioNet BIDMC ECG + annotations] --> M[bidmc_hr_check.py]
    N[Offline synthetic ECG<br/>self-test mode] --> M
    M --> O[HR error report<br/>Markdown and CSV]
```

### Two ways measurements can enter the detector

-   **Simulation path:** `generate_data.py` produces daily measurements
    directly. This is useful for stress-testing the warning logic
    against known simulated events.
-   **Hardware path:** the chair records raw waveforms and `pipeline.py`
    extracts daily measurements. Those measurements can then be passed
    to `detector.py`.

The simulation path and hardware path serve different purposes.
Synthetic data makes it possible to evaluate many controlled scenarios;
waveform processing tests whether raw signals can be converted into
plausible measurements. The BIDMC script separately evaluates the
heart-rate extraction method on annotated ECG data.

------------------------------------------------------------------------

## Repository workflow

A typical repository layout may look like this:

``` text
saathi/
├── generate_data.py
├── pipeline.py
├── detector.py
├── bidmc_hr_check.py
├── data/
│   ├── daily.csv
│   ├── truth.csv
│   └── patients.csv
└── reports/
    ├── bidmc_hr_report.md
    └── bidmc_hr_results.csv
```

The `data/` and `reports/` files are generated outputs; they may not
exist until the relevant script has been run. Exact output paths and
command-line options should be confirmed against the version of each
script in the repository.

------------------------------------------------------------------------

## Script 1: `generate_data.py`

### Purpose

This script creates a synthetic cohort of older adults over a
configurable number of days. It provides a repeatable test environment
before real patient data is available. The simulation intentionally
includes ordinary day-to-day variation, sensor failures, missed checks,
and illnesses that can resemble heart-failure deterioration.

It is not a clinical data generator validated against a real population.
Its parameters encode assumptions chosen for software testing;
conclusions drawn from it are only as realistic as those assumptions.

### Main stages

``` mermaid
flowchart TD
    A[Command-line configuration<br/>cohort size, days, random seed] --> B[Create patient profiles]
    B --> C[Assign event status and phenotype]
    C --> D[Generate daily severity trajectory]
    D --> E[Apply physiological changes]
    E --> F[Add confounders, drift, noise, and missing readings]
    F --> G[Write daily.csv]
    F --> H[Write truth.csv]
    B --> I[Write patients.csv]
```

### 1. Patient profiles and personal baselines

Each simulated patient is assigned baseline values for variables such
as:

-   `weight_kg` --- body weight in kilograms.
-   `foot_z_ohm` --- foot bioimpedance in ohms.
-   `rest_hr_bpm` --- resting heart rate in beats per minute.
-   `spo2_pct` --- estimated peripheral oxygen saturation.
-   `resp_rate_bpm` --- respiratory rate in breaths per minute.
-   `ptt_ms` --- pulse transit time in milliseconds.
-   `sit_count` --- a proxy for how often the chair is used.

The values vary between people so the detector cannot succeed merely by
applying one universal threshold to everyone.

The simulation also models patient and behavior traits, including a
preferred measurement time, a tendency to miss checks, and the
probability of reporting symptoms. A subset of patients is marked as
having a pacemaker or implantable cardioverter-defibrillator (ICD),
which affects whether the simulated impedance measurement is considered
available.

### 2. Simulated worsening phenotypes

The described default cohort assigns an event to about 60% of patients.
Among simulated events, three patterns are represented:

  ------------------------------------------------------------------------
  Phenotype              Approximate share of events Intended pattern
  --------------------- ---------------------------- ---------------------
  `congestive`                                   60% More typical
                                                     fluid-retention
                                                     pattern: weight tends
                                                     to rise and impedance
                                                     tends to fall.

  `no_weight_gain`                               20% Cardiopulmonary
                                                     changes can occur
                                                     without substantial
                                                     weight gain.

  `subtle`                                       20% Smaller changes
                                                     across several
                                                     measurements, making
                                                     detection more
                                                     difficult.
  ------------------------------------------------------------------------

The event is assigned a simulated admission day, with worsening
beginning several days earlier. A severity variable (`sev`) progresses
from approximately 0 toward 1 during the pre-admission window. The shape
can be changed---for example, a linear progression, an accelerating
power curve, a late surge, or a step-like change.

At full simulated severity, the phenotype parameters scale changes to
the patient's baseline. For example, a congestive trajectory may add
weight and heart rate, reduce foot impedance, and increase respiratory
rate. The exact ranges are configuration assumptions, not diagnostic
cut-offs.

### 3. Confounders and imperfect measurements

The generator adds events that can mimic or obscure deterioration:

-   **Salty meals or festivals:** short-lived weight increase and a
    small impedance reduction.
-   **Fever or infection:** heart rate and respiratory rate increase,
    oxygen saturation may fall, and breathlessness may become more
    likely.
-   **Diuretic dose change:** a lasting weight decrease and impedance
    increase are simulated.
-   **Heavy clothing:** a one-day apparent weight increase.
-   **Time of day:** evening measurements can be heavier than morning
    measurements.
-   **Natural drift:** gradual random changes in weight and heart rate.
-   **Sensor noise:** random variation is added to measurements.
-   **Sensor failures:** a measurement may be missing, represented by
    `NaN`.
-   **Missed checks:** the patient may not complete a daily session,
    with the probability potentially increasing during deterioration.

These cases matter because a robust detector should not treat every
abnormal-looking reading as evidence of heart failure, and should not
treat missing data as proof that the patient is well.

### 4. Output files

-   **`data/daily.csv`** --- the simulated daily observations intended
    to be visible to the detector.
-   **`data/truth.csv`** --- hidden simulated event labels and
    confounder information used for evaluation.
-   **`data/patients.csv`** --- patient metadata and simulation labels.

Keep `truth.csv` out of the detector's feature inputs. It is an
evaluation reference; using it to score a patient would leak the answer
into the algorithm and invalidate the evaluation.

### Why this script exists

It lets the team test edge cases deliberately, repeat experiments using
a fixed random seed, compare parameter settings, and measure whether
changes improve sensitivity without producing too many false alarms.
Results on synthetic data should be described as simulation
results---not as clinical validation.

------------------------------------------------------------------------

## Script 2: `pipeline.py`

### Purpose

`pipeline.py` is the signal-processing layer between the chair's raw
sensor signals and the daily measurements used by the detector. A
typical session lasts 90 seconds. The pipeline filters signals,
estimates measurements, checks signal quality, and rejects readings that
do not meet configured quality criteria.

The intention is to provide a clear reference implementation that could
later be reimplemented in C/C++ for an ESP32. A port should be checked
against the Python implementation using the same test recordings;
equivalent-looking filters can differ because of sampling rates, numeric
precision, and library conventions.

### Main stages

``` mermaid
flowchart TD
    A[90-second raw session] --> B[Discard initial settling interval]
    B --> C[Load-cell processing]
    B --> D[Foot bioimpedance processing]
    B --> E[Seat BCG processing]
    B --> F[Finger PPG processing]

    C --> G[Quality-controlled weight]
    D --> H[Quality-controlled impedance]
    E --> I[BCG heart rate and J-wave times]
    F --> J[PPG heart rate and pulse feet]
    F --> K[SpO2 estimate]
    F --> L[Respiratory-rate estimate]

    I --> M[Heart-rate fusion]
    J --> M
    I --> N[BCG-to-PPG timing]
    J --> N
    M --> O[Daily measurements + quality/missing values]
    G --> O
    H --> O
    K --> O
    L --> O
    N --> O
```

### 1. Settling interval

The initial portion of the recording is discarded (described
configuration: 6 seconds). Sitting down can cause mechanical transients
and unstable contact. Removing this interval reduces the chance that a
settling spike is mistaken for a physiological event.

### 2. Weight from load cells

The four load-cell channels are combined into a total-load signal. The
algorithm searches for a steady window---described as a 10-second window
advanced in one-second steps---and evaluates its variability.

The described quality rules include:

-   A candidate stable window has a standard deviation no greater than
    0.5 kg.
-   The patient must sustain the full-load plateau for at least 50% of
    the relevant session.
-   The tare-corrected weight must be within a configured plausible
    range, described as 25--200 kg.

The tare is the chair's unloaded reading and must be measured and
maintained correctly. If stability or range checks fail, the pipeline
should reject the weight rather than emit a plausible-looking but
untrustworthy value.

### 3. Foot bioimpedance

The pipeline summarizes the foot impedance readings using a median. The
described checks accept readings only when:

-   The median is within 200--1500 ohms.
-   The coefficient of variation (`standard deviation / mean`) is below
    5%.

These checks aim to identify unstable contact or implausible readings.
Socks, dry skin, foot placement, electrode design, excitation frequency,
and the analog front-end can all affect impedance. The thresholds must
therefore be verified with the actual hardware and safety design.

**Implant safety note:** the design description disables the 50 kHz
impedance measurement for patients with a pacemaker/ICD. This should be
treated as a conservative design requirement pending formal
electrical-safety review---not as a substitute for manufacturer guidance
or clinical approval.

### 4. Filtering and beat detection

The pipeline uses Butterworth filters to isolate frequency bands
relevant to the signal being processed. Where zero-phase filtering is
used offline, forward/backward filtering avoids phase delay, but it is
not directly equivalent to a real-time embedded filter. A real-time
ESP32 implementation will need a suitable causal filter and its timing
effects must be accounted for.

The described beat detector:

1.  Uses a dynamic threshold based on 40% of the signal's
    98th-percentile amplitude.
2.  Applies a minimum time gap between detected peaks to reduce
    duplicate detections.
3.  Calculates inter-beat intervals (IBIs) from adjacent peak times.
4.  Retains intervals corresponding to a configured physiological
    heart-rate range.
5.  Uses the median IBI to estimate BPM:
    `heart_rate_bpm = 60 / median_ibi_seconds`.
6.  Calculates a beat-consistency quality measure from the fraction of
    intervals near the median.

The exact minimum peak gap differs by use case in the supplied design
notes (approximately 0.30--0.33 seconds); check the actual function
defaults before relying on a particular maximum-rate limit.

### 5. Seat BCG heart rate

Ballistocardiography (BCG) measures small mechanical movements
associated with cardiac ejection. The described pipeline filters the
seat signal between roughly 1 and 10 Hz and detects prominent cardiac
J-wave-related peaks. It returns an estimated heart rate, a quality
measure, and peak timestamps.

BCG is sensitive to posture, body movement, chair mechanics, and how the
person contacts the seat. Its quality score is therefore important; a
numerical estimate alone does not guarantee a valid measurement.

### 6. Finger PPG heart rate and pulse feet

Photoplethysmography (PPG) uses changes in red and infrared light
detected at the finger to measure pulsatile blood-volume changes.

The pipeline first checks that infrared counts are high enough for most
of the recording, as a proxy for finger presence and adequate optical
signal. It then filters the PPG waveform and detects pulse peaks for
heart-rate estimation.

For pulse-onset timing, the design uses an **intersecting-tangent
method**: within a pre-peak window it locates the waveform minimum,
finds the steepest rising slope, and extrapolates a tangent to estimate
the pulse foot. This can be more appropriate for timing than simply
taking the minimum of a high-pass-filtered waveform, because filtering
can shift waveform landmarks.

The accuracy of pulse-foot timing depends on sampling rate, filtering,
pulse morphology, motion, and correct pairing with the BCG event.

### 7. Heart-rate fusion

The pipeline compares BCG and PPG heart-rate estimates:

-   If both quality scores meet the configured minimum and estimates
    agree within the configured tolerance (described as 5 BPM), it takes
    their arithmetic mean.
-   If they disagree or one estimate is poor quality, it selects the
    estimate with the better quality score, subject to the code's
    fallback rules.
-   If neither estimate is usable, the heart rate should be missing
    rather than fabricated.

This is a rule-based fusion strategy, not a trained model. Equal
averaging is used only in the agreement case; otherwise the selection is
quality-based.

### 8. SpO2 estimate

For each accepted PPG beat, the pipeline calculates a red/infrared
ratio-of-ratios:

`R = (AC_red / DC_red) / (AC_ir / DC_ir)`

Here, `AC` represents the pulsatile component and `DC` the baseline
component for each wavelength. It then aggregates valid beat ratios and
applies the described prototype calibration:

`SpO2_estimate = 110 - 25 × median(R)`

The design includes quality gates such as a minimum number of valid
beats, sufficient beat consistency, and a limited interquartile spread
between beat-level ratios. A result may be clipped to the configured
output range.

**This linear equation is a prototype calibration, not a universally
valid clinical calibration.** A MAX30102's raw optical data cannot by
itself guarantee medical-grade oxygen saturation. Clinical SpO2 requires
validation against an appropriate reference and consideration of skin
pigmentation, perfusion, motion, sensor placement, and other factors.

### 9. Respiratory rate from PPG

Breathing can modulate a PPG signal in several ways:

-   **RIIV:** respiratory-induced intensity variation---the waveform
    baseline changes with breathing.
-   **RIAV:** respiratory-induced amplitude variation---the pulse
    amplitude changes with breathing.
-   **RIFV:** respiratory-induced frequency variation---beat intervals
    vary with respiration.

The described method builds a beat-by-beat series for each modulation,
interpolates the series onto a regular 4 Hz grid, applies a
respiratory-frequency band-pass (approximately 0.1--0.6 Hz, or 6--36
breaths/min), and uses Welch power spectral density to find a dominant
frequency.

It reports a respiratory rate only when at least two methods agree
within a configured tolerance (described as 2.5 breaths/min), then
combines the agreeing estimates using their median. This agreement gate
is intended to reduce spurious results.

### 10. Pulse transit time (PTT)

The pipeline pairs a BCG event associated with cardiac ejection with the
following PPG pulse foot. The delay is treated as a PTT estimate. The
described implementation accepts delays around 100--400 ms and requires
a minimum number of valid pairs before returning the median.

PTT is influenced by vascular properties, blood pressure, timing
definitions, sensor placement, and signal-processing delay. It should be
treated as an exploratory feature unless validated for the intended use.

### Why this script exists

The detector should work with daily measurements, not raw waveforms.
`pipeline.py` centralizes the signal processing and quality checks so
that invalid contact, movement, or sensor failure is represented as a
missing measurement rather than a false clinical change.

------------------------------------------------------------------------

## Script 3: `detector.py`

### Purpose

`detector.py` is a rule-based, explainable early-warning algorithm. It
builds an individual baseline, measures how today's readings deviate
from that baseline, accumulates weighted evidence across sensors and
symptom questions, and applies alert rules.

It is not a diagnosis model. Its scores and thresholds are engineering
hypotheses that need validation and calibration against representative
clinical data before any clinical use.

### Main stages

``` mermaid
flowchart TD
    A[Daily measurement row] --> B[Integrity and missing-check guards]
    B --> C[Reject or flag suspect readings]
    C --> D[Correct weight for test time]
    D --> E[Build eligible personal baseline]
    E --> F[Median and robust spread using MAD]
    F --> G[Directional deviation score per signal]
    G --> H[Apply signal weights and symptom points]
    H --> I[Combined daily risk score]
    I --> J{Alert rules satisfied?}
    J -->|Yes| K[Alert + plain-language reasons]
    J -->|No| L[No alert / continue monitoring]
    K --> M[Evaluation against hidden truth labels]
    L --> M
```

### 1. Integrity checks and missing measurements

Before scoring, the detector checks whether a daily measurement should
be trusted.

-   **Missed checks:** records missing sessions and can mark a check
    overdue after repeated missed days.
-   **Learning period:** the first 14 days are treated as
    baseline-learning days in the described configuration.
-   **Weight/foot-contact cross-check:** if impedance is missing on a
    completed check, weight may be rejected because the feet may not
    have been on the intended footrest. A configured implant exception
    is described in the design.
-   **Identity guard:** if the time-adjusted weight differs from the
    patient's baseline by more than 8 kg, and identity has not been
    confirmed, the status becomes `identity_uncertain`. Weight is
    excluded from scoring/baseline updates for that day, but other valid
    signals can still contribute.
-   **Missing values:** missing or rejected readings should not be
    interpreted as normal readings.

The identity guard is a heuristic. It cannot reliably identify who sat
in the chair in all circumstances and should be supplemented by an
appropriate identity-confirmation process if required.

### 2. Time-of-day weight correction

Body weight can change over the course of a day because of food and
fluid intake. The design estimates a cohort-level diurnal slope using
training data from normal days and adjusts weight readings to a
reference measurement time:

`weight_adj = weight_kg - slope × (test_hour - reference_hour)`

The generator's described nominal drift is about 0.07 kg/hour after 7
AM, but the detector is intended to learn a slope from training data
rather than assume that every person has exactly that rate. The
reference hour is based on the patient's usual measurement time.

### 3. Personal baseline

The described baseline uses the first 14 days and later eligible
low-risk days, with an 11-day lag before newer readings can enter the
pool. Up to 28 recent valid days are used.

The lag is intended to reduce the risk of slowly worsening measurements
being absorbed into the definition of "normal." It is a design choice,
not a guarantee: baseline contamination remains possible if the
deterioration lasts longer than the quarantine period or if eligibility
rules are too permissive.

For each signal, the baseline center is the median. Its robust spread is
based on the median absolute deviation (MAD):

`spread = max(1.4826 × median(abs(x - median(x))), minimum_floor)`

The factor 1.4826 makes MAD comparable to standard deviation for
normally distributed data. The floor prevents an unusually stable
baseline from producing arbitrarily large scores from tiny fluctuations.

For relative-spread signals, the floor is scaled by the magnitude of the
baseline median; for absolute-spread signals, it is in the signal's own
units.

### 4. Directional deviations and weighted score

Each signal has a direction indicating which change is considered
concerning. Rising weight, heart rate, and respiratory rate are scored
in the positive direction; falling impedance and SpO2 are scored in the
negative direction. PTT is treated as two-sided because the direction of
a meaningful change is less certain in this prototype.

For a directional signal:

`z = direction × (today - baseline_median) / baseline_spread`

For a two-sided signal, the magnitude of the standardized deviation is
used. Deviations within one spread receive no points. Beyond that, the
contribution increases and is capped:

`points = clip(z - 1, 0, 3) × signal_weight`

This design has three purposes:

1.  **Personalization:** compare a patient with their own baseline
    rather than a universal population threshold.
2.  **Deadband:** avoid scoring small deviations within one robust
    spread.
3.  **Cap:** limit the influence of a single extreme sensor reading.

Symptom answers add a fixed amount for each "Yes" response in the
described configuration.

### 5. Alert rules

The described detector combines a weighted score with direct
safety-oriented triggers:

-   **Sustained elevated score:** the score meets or exceeds a
    configured threshold on two consecutive measured days.
-   **Rapid weight gain:** adjusted weight rises by at least 2 kg
    relative to the recent three-day minimum.
-   **Relative SpO2 decline:** oxygen saturation is at least 4
    percentage points below the personal baseline median on two
    consecutive measured days.
-   **High score followed by missed checks:** a high-score day followed
    by two missed days can trigger an alert rather than assuming the
    patient is safe.

Exact status names, threshold inclusivity, and the meaning of
"consecutive" should be checked in the code. Missing readings and missed
sessions are distinct: one sensor may fail during a completed session,
whereas a missed check means the session itself was not completed.

### 6. Explainable reasons

When an alert is produced, the detector can report the leading
contributing observations in plain language---for example, that weight
is above usual or foot impedance is below usual. These statements
describe measured changes; they should not claim that heart failure has
been diagnosed.

### 7. Evaluation

When ground-truth labels are available, the evaluation code can
calculate metrics such as:

-   **Sensitivity:** proportion of simulated event cases detected.
-   **Specificity:** proportion of non-event cases correctly left
    without an event alert, according to the evaluation definition.
-   **Lead time:** time between the first qualifying alert and the
    simulated admission date.
-   **False-alarm episodes per patient-month:** false alerts grouped
    into episodes according to a configured gap rule.
-   **Wilson confidence intervals:** uncertainty intervals for
    proportions such as sensitivity or specificity.

The exact event window, alert-to-event matching rules, and false-alarm
grouping must be documented and held consistent when comparing versions.
A high sensitivity on synthetic data does not establish clinical
sensitivity.

### Why this script exists

A patient's normal heart rate, weight, oxygen saturation, and activity
can differ substantially from another person's. Personal baselines and
multi-sensor evidence make the system more adaptable and more
explainable than fixed thresholds alone.

------------------------------------------------------------------------

## Script 4: `bidmc_hr_check.py`

### Purpose

This script tests the heart-rate extraction logic used by `pipeline.py`
against annotated ECG recordings from the PhysioNet BIDMC Congestive
Heart Failure Database (`chfdb`). It focuses on the beat-detection and
median-inter-beat-interval calculation, not on validating every chair
sensor or the full ADHF warning system.

The described configuration samples 40 randomly selected 90-second
windows from each of 15 records, giving up to 600 windows when all
records and windows are usable.

### Main stages

``` mermaid
flowchart TD
    A[Select mode] --> B[PhysioNet ECG records]
    A --> C[Offline synthetic ECG self-test]
    B --> D[Read ECG and expert annotations]
    D --> E[Select random 90-second windows]
    E --> F[Filter ECG and emphasize QRS peaks]
    F --> G[Shared pipeline beat detector and HR calculation]
    D --> H[Reference HR from annotated beat times]
    G --> I[Compare estimate with reference]
    H --> I
    C --> F
    I --> J[MAE, bias, and within-5-BPM rate]
    J --> K[Markdown and CSV reports]
```

### 1. ECG preprocessing and estimated heart rate

The script adapts the signal to ECG: it band-passes the waveform over a
QRS-focused frequency range (described as 5--20 Hz), squares the
filtered signal so negative and positive complexes produce positive
energy peaks, and passes it to the shared beat detector and heart-rate
calculation.

Reusing the same core functions helps test the beat-detection logic, but
the ECG is not the same signal as BCG or PPG. Good performance on ECG
therefore does not automatically prove equivalent performance on the
chair's mechanical or optical signals.

### 2. Reference heart rate

The database contains expert beat annotations. The script selects the
annotations considered valid beats, restricts them to each selected time
window, and calculates a reference heart rate from the median time
between annotated beats:

`reference_bpm = 60 / median(diff(annotation_times_in_seconds))`

The exact annotation symbols accepted and excluded should be verified
against the database documentation and the code. Noise markers are not
beats and should not be counted as such.

### 3. Metrics

The described report includes:

-   **Mean absolute error (MAE):** average absolute difference between
    estimated and reference BPM.
-   **Bias:** average signed difference (`estimate - reference`),
    indicating systematic over- or under-estimation.
-   **Within ±5 BPM:** percentage of evaluated windows whose estimate
    differs from reference by no more than 5 BPM.
-   **Per-record results:** helps identify records where performance
    differs.

Results should include the number of valid windows and any
rejected/failed windows. Excluding difficult windows without reporting
how many were excluded can make performance appear better than it is.

### 4. Offline self-test

A self-test mode can generate synthetic ECG-like waveforms with
QRS-shaped pulses, noise, respiratory baseline variation, and
beat-to-beat variability. This checks that the code path runs without
downloading records.

A synthetic self-test is a software sanity check only. It is not a
substitute for evaluation on real annotated ECG recordings.

### Why this script exists

It provides a reproducible, quantitative check of the shared
beat-detection and median-IBI calculation against an independent
reference. It is a narrow validation module; it does not validate the
SpO2 calibration, respiratory-rate estimate, PTT, foot impedance, weight
sensing, detector alert rules, or clinical benefit.

------------------------------------------------------------------------

## How the scoring weights work

The word "weight" is used in several different ways in this project.
They should not be confused.

### A. Detector signal weights

The described `detector.py` configuration uses the following weights in
the daily risk score:

  --------------------------------------------------------------------------
  Signal        Worsening        Score weight   Maximum sensor Minimum
                direction                               points spread floor
                                                (`3 × weight`) 
  ------------- ------------ ---------------- ---------------- -------------
  Adjusted      Higher                   1.00             3.00 0.40 kg
  weight                                                       

  Foot          Lower                    1.00             3.00 2% of
  impedance                                                    baseline
                                                               magnitude

  Resting heart Higher                   0.70             2.10 3 BPM
  rate                                                         

  Respiratory   Higher                   0.70             2.10 2 breaths/min
  rate                                                         

  SpO2          Lower                    0.50             1.50 1 percentage
                                                               point

  PTT           Either                   0.25             0.75 8 ms
                direction                                      

  Chair sit     Lower                    0.25             0.75 20% of
  count                                                        baseline
                                                               magnitude
  --------------------------------------------------------------------------

These are manually configured algorithm weights, not learned
coefficients and not universal measures of clinical importance. The
point cap applies to each sensor's continuous contribution.

The three symptom questions---orthopnea, breathlessness, and increased
foot/ankle swelling---are described as adding **0.70 points per "Yes"**.
They are direct additions, not Z-score-based contributions.

### B. Pipeline thresholds and combining rules

`pipeline.py` mostly uses filters, quality gates, and robust summaries
rather than a single weighted sum:

-   Four load-cell channels are combined to represent total load.
-   BCG and PPG heart rates are averaged equally only when both are high
    enough quality and agree within the configured tolerance.
-   If heart-rate estimates disagree, the described fallback selects the
    higher-quality estimate.
-   Respiratory-rate estimates from RIIV, RIAV, and RIFV are combined
    using agreement checks and a median, not a fixed weighted average.
-   SpO2 uses a prototype linear calibration formula; the coefficient
    `-25` is a calibration slope, not a clinical risk weight.
-   The beat detector's 40% threshold multiplier is a detection
    parameter, not a percentage contribution to the final clinical
    score.

### C. Generator probabilities and effect sizes

`generate_data.py` uses probabilities to decide which simulated events
occur, and effect sizes to determine how strongly each event changes
measurements:

-   About 60% event probability is a simulation setting.
-   Phenotype proportions determine which pattern a simulated event
    follows.
-   Severity (`sev`) scales an event's effect over time.
-   Confounder probabilities determine how often synthetic events such
    as infection or diuretic changes occur.
-   Noise standard deviations determine how much random measurement
    variation is added.

These are simulation parameters, not weights applied by the detector.
The detector's scoring weights and the generator's effect-size ranges
serve different purposes.

------------------------------------------------------------------------

## Expected data files

### `daily.csv`

Contains one row per patient-day or scheduled daily observation,
depending on the implementation. Typical columns include:

  -----------------------------------------------------------------------
  Column                              Meaning
  ----------------------------------- -----------------------------------
  Patient identifier                  Stable ID linking rows to a
                                      simulated patient

  Day/date                            Position in the simulated timeline

  `weight_kg`                         Measured or simulated weight

  `foot_z_ohm`                        Foot bioimpedance

  `rest_hr_bpm`                       Resting heart rate

  `spo2_pct`                          Estimated oxygen saturation

  `resp_rate_bpm`                     Respiratory rate

  `ptt_ms`                            Pulse transit time

  `sit_count`                         Chair-use proxy

  Symptom fields                      Daily yes/no responses, if enabled

  Measurement-time and check-status   Used for time correction and
  fields                              missed-check handling
  -----------------------------------------------------------------------

Column names and required fields are determined by the actual script
version; check the source before using a hand-created CSV.

### `truth.csv`

Contains evaluation-only labels, such as whether an event is active and
which confounders were injected. It should not be passed into the
feature-scoring stage.

### `patients.csv`

Contains patient-level simulation metadata such as baseline
characteristics, assigned phenotype, event/admission timing, and
simulation traits. Some columns may be intentionally hidden from the
detector.

### Reports

The BIDMC validation script is described as writing:

-   `reports/bidmc_hr_report.md`
-   `reports/bidmc_hr_results.csv`

Create the output directory if the script does not do so automatically.

------------------------------------------------------------------------

## Installation

Use a virtual environment to keep dependencies isolated.

### Windows (Git Bash)

``` bash
python -m venv .venv
source .venv/Scripts/activate
python -m pip install --upgrade pip
```

### macOS / Linux

``` bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
```

The described scripts use common scientific Python libraries such as:

-   `numpy`
-   `pandas`
-   `scipy`
-   `matplotlib` (if plotting is enabled)
-   `wfdb` (for reading PhysioNet waveform records in the validation
    script)

Install the dependencies actually imported by your checked-out source.
If the repository includes a `requirements.txt`, prefer:

``` bash
python -m pip install -r requirements.txt
```

Otherwise, a starting installation for the described functionality is:

``` bash
python -m pip install numpy pandas scipy wfdb
```

Do not commit your virtual environment (`.venv/`) or local patient data.
Add them to `.gitignore`.

------------------------------------------------------------------------

## Running the scripts

The examples below reflect the options described for this project. Run
each script with `--help` first and use the exact flags exposed by your
checked-out version.

### 1. Generate a reproducible synthetic cohort

``` bash
python generate_data.py --n 120 --days 60 --seed 26
```

Optional stress-test arguments are described for changing lead time,
trajectory shape, missed-check behavior, drift, event effect size,
confounder frequency, and measurement noise. Check
`python generate_data.py --help` for exact names and accepted values.

### 2. Evaluate the detector on synthetic data

The exact CLI for `detector.py` should be confirmed from the source.
Conceptually, the workflow is:

``` text
daily.csv + detector configuration
              |
              v
      detector.py
              |
              +--> daily scores and alerts
              +--> evaluation against truth.csv
```

Make sure the detector uses `daily.csv` for its observations and
accesses `truth.csv` only in the evaluation stage. Keep patient IDs and
event windows aligned when joining files.

### 3. Process a raw chair recording

`pipeline.py` is described as a reference processing module. Its exact
command-line interface and raw recording format depend on the
implementation. Check its function signatures and any example data
before running it. The expected conceptual input is a 90-second session
containing the load-cell, impedance, BCG, red PPG, and infrared PPG
channels with their sampling times or sampling rates.

### 4. Validate heart-rate estimation

For an offline code-path check, use the self-test flag if supported:

``` bash
python bidmc_hr_check.py --selftest
```

For database validation, run the script in the mode documented by its
`--help` output. Internet access and the `wfdb` dependency may be needed
to retrieve PhysioNet records.

Do not report a successful clinical validation unless real database
records were actually loaded, annotations were used as the reference,
and the resulting report was inspected.

------------------------------------------------------------------------

## Interpreting the results

-   **A missing value (`NaN`) is not zero.** It means that the signal
    was unavailable or failed a quality gate.
-   **A high score is a prompt for review, not a diagnosis.** The reason
    text should describe the observations that contributed to the score.
-   **Sensitivity and specificity depend on evaluation definitions.**
    State the event window, alert matching rule, and handling of missing
    data.
-   **Lead time is not the same as guaranteed advance warning.** It is
    calculated relative to the simulated or recorded event date under a
    particular evaluation protocol.
-   **False alarms should be reported as episodes as well as individual
    alert days.** Consecutive alerts can belong to one episode.
-   **Validation is modular.** ECG heart-rate performance does not
    validate PPG SpO2, respiratory rate, PTT, impedance, weight, or the
    full alert system.
-   **Use confidence intervals and report sample sizes.** A point
    estimate alone can conceal substantial uncertainty.

------------------------------------------------------------------------

## Engineering assumptions and limitations

1.  **Synthetic data are not clinical evidence.** The simulator is
    useful for controlled testing but cannot reproduce every source of
    patient variability or real-world failure.
2.  **The detector weights are provisional.** Their rationale should be
    documented and their performance tested on independent,
    representative data.
3.  **Sensor quality matters as much as the scoring model.** Bad contact
    or motion can create plausible-looking but wrong readings.
4.  **Offline and embedded filtering differ.** Zero-phase filters
    require future samples and cannot be copied directly into a
    real-time microcontroller pipeline without redesign.
5.  **SpO2 calibration requires validation.** A simple
    ratio-to-saturation equation is not sufficient to claim
    clinical-grade accuracy.
6.  **Impedance excitation requires safety review.** Patients with
    implanted cardiac devices need an appropriate, clinically reviewed
    sensing policy.
7.  **Missingness can be informative but ambiguous.** A missed check
    could reflect illness, routine disruption, sensor problems, or many
    other causes.
8.  **The scripts are not a replacement for clinical care.** They must
    not be used to independently diagnose, treat, or rule out heart
    failure.

------------------------------------------------------------------------

## Suggested development sequence

1.  Run the generator with a fixed seed and inspect the three output
    CSVs.
2.  Verify that `truth.csv` is excluded from the detector's feature
    inputs.
3.  Run the detector and inspect individual patient timelines, scores,
    alerts, and false-alarm episodes---not just aggregate metrics.
4.  Test `pipeline.py` on recorded waveforms with known sampling rates
    and inspect intermediate signals and quality flags.
5.  Run the BIDMC validation on real annotated records, recording the
    number of usable and rejected windows.
6.  Compare the Python reference pipeline with any ESP32 port using the
    same waveforms.
7.  Document every threshold and calibration change, and evaluate
    changes on data that were not used to tune them.

------------------------------------------------------------------------

## Acknowledgements and data provenance

The heart-rate validation module is described as using the **BIDMC
Congestive Heart Failure Database (`chfdb`) hosted by PhysioNet**.
Before redistribution or publication, review the dataset's current
access conditions, citation instructions, and license.

The synthetic cohort is generated by project code and should be clearly
labelled as synthetic in every report or demonstration.

------------------------------------------------------------------------

## Project status

SAATHI combines a synthetic-data test harness, a sensor-processing
reference pipeline, a rule-based personal-baseline detector, and a
focused heart-rate validation script. Together, these modules support
iterative engineering and evaluation. Claims about real-world early
detection, clinical accuracy, or reduced hospital admissions require
evidence beyond the software architecture and simulation results
described here.
