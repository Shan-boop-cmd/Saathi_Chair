# Heart-rate method self-test (synthetic ECG, offline)

40 windows of 90 s from 1 record(s).

| Measure | Value |
| --- | --- |
| Mean absolute error | 0.10 bpm |
| Bias | +0.01 bpm |
| Windows within ±5 bpm | 100% |
| Windows with no estimate | 0 |

Per record:

| Record | Windows | Mean abs. error (bpm) | Within ±5 bpm |
| --- | --- | --- | --- |
| synthetic | 40 | 0.10 | 100% |

Heart-failure ECGs include extra beats and irregular rhythms; the median beat interval keeps the estimate stable, but irregular windows are where errors concentrate.