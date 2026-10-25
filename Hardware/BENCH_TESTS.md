# SAATHI chair: bench tests (to turn simulations into measurements)

Nothing in this project has been measured on a physical chair yet. These tests are the next step. Each one says what to
measure, how, what counts as a pass, and what to change if it fails. Record every result in the table at the end and put
it in the report. Serial console at 115200 baud; bench commands: `weigh`, `zread`, `bcg`, `status`, `check`.

Do them in this order: each later test depends on the earlier ones.

## 1. Weight (load cells), about 1 hour

**Why the design should work:** the four legs carry everything the chair carries. Leaning back, gripping the armrests or
shifting position moves force between the legs but not off them, so the sum stays the same. The footrest hangs from the
front legs, so the feet are weighed too. The exception is anything that touches the floor or a wall: feet on the floor,
or a walking stick leaning on the floor.

**Expected error (datasheet-based):** each cheap 50 kg cell is about ±0.05% of full scale for non-linearity (±25 g) and
±0.05% for 30-minute creep. Four cells add about ±0.1 kg. HX711 noise at 10 samples/s is about ±20 g after averaging.
Temperature drift of the zero is about 25 g per 10 °C per cell, which the firmware's auto-zero of the empty chair
removes. **Target: within ±0.3 kg of a reference scale and repeatable to ±0.2 kg.** The detector's minimum spread for
weight is 0.4 kg.

| Step | How | Pass |
| --- | --- | --- |
| 1a | Empty chair: `tare`. Put 20, 40 and 60 kg of known weight on the seat (sandbags or water cans weighed on a shop scale). `weigh` at each. | error ≤ 0.3 kg at each load |
| 1b | Calibrate: `cal 60` with 60 kg on the seat. Repeat 1a. | error ≤ 0.3 kg |
| 1c | A team member weighs on a reference digital scale, then sits 10 times (stand up between). `check` each time. | spread of the 10 readings ≤ 0.4 kg; mean within 0.5 kg of the reference |
| 1d | Same person: upright, leaning back, gripping the armrests, feet on the footrest. | all within 0.3 kg of each other |
| 1e | Same person, feet on the floor instead of the footrest. | reading drops by about the feet's weight (2–3 kg). The firmware notices: no foot-impedance contact gives a "put your feet on the plates" prompt |
| 1f | Leave 60 kg on the seat for 30 minutes; `weigh` every 5 minutes. | creep ≤ 0.2 kg |

If it fails: check that each leg cup touches the floor only through the load cell (`cad/README.md`), check the bridge
wiring (`hardware/README.md`), then recalibrate.

## 2. Seat heartbeat (BCG), about 2 hours

The design assumes a 20 mV heartbeat (J-wave) at the film, but the simulation sweep (`bcg_amplitude_sweep.py`) shows
the default gain finds the right heart rate for film signals from 2 to 100 mV.

| Step | How | Pass |
| --- | --- | --- |
| 2a | Film under the cushion, team member sits still. `bcg`. | swing at the ADC between 60 and 2,800 mV. Below 150 mV: fit the high-gain option (3.3 MΩ with 3.3 nF). Above 2,800 mV: halve the gain (470 kΩ with 22 nF) |
| 2b | Same, with a phone pulse-oximeter or fitness watch on the wrist for reference. `check` 5 times. | heart rate within ±5 bpm of the reference in at least 4 of 5 checks |
| 2c | Repeat 2b with 3 different team members (light, medium, heavy) and 2 cushion thicknesses. | same as 2b; note which combinations fail |
| 2d | Small movements during the check (talking, shifting). | the firmware rejects the window (heart rate from PPG instead), or the error stays within ±5 bpm |

Report: the valid-beat rate, heart-rate error and number of rejected checks, as the review asked.

## 3. Foot impedance, about 1 hour (battery power only)

| Step | How | Pass |
| --- | --- | --- |
| 3a | Charger unplugged. Put a 500 Ω 1% resistor between the left and right toe plates, and the heel plates joined to the toes on each side (shorting links). `zcal 500`. | factor between 0.8 and 1.25 |
| 3b | Replace with 390, 470, 620 and 680 Ω. `zread`. | within ±2% of the resistor value |
| 3c | Body-like load: 500 Ω in parallel with 4.7 nF. `zread`. | within ±3% of the resistor |
| 3d | Add 1 kΩ and then 2.2 kΩ in series with each plate (bad skin contact). `zread`. | changes the reading by less than 2% |
| 3e | Plug the charger in. `zread`. | returns NAN (blocked) |
| 3f | Measure the 50 kHz current with a 1 kΩ resistor in place of the feet and an oscilloscope across it. | below 25 µA rms |
| 3g | Team members, bare feet, 5 readings each on 3 days. | day-to-day spread for the same person under 3% |

Step 3g is the first look at whether foot impedance is repeatable enough to matter. If the spread is above the expected
3–6% fluid effect, keep foot impedance as exploratory or build the Lite version without it (₹4,379).

## 4. Power, about half a day (mostly waiting)

| Step | How | Pass |
| --- | --- | --- |
| 4a | USB power meter on the 5 V rail, or a multimeter in series with the battery. Measure idle (asleep), during `check`, during voice, during an SMS. | close to `hardware/power_budget.json` (idle ~7 mA from the battery) |
| 4b | During an SMS, watch the battery voltage with an oscilloscope or a fast multimeter. | no ESP32 reset (no "SAATHI chair, built" line reprinted); dips stay above 3.4 V |
| 4c | Full charge, unplug, leave idle with one `check` a day. | the chair is still working after 7 days (the estimate is about 20) |
| 4d | Unplug mid-check. | after re-plugging, history is intact (`dump`) and the queued SMS is sent |

## 5. Messages, about 1 hour

| Step | How | Pass |
| --- | --- | --- |
| 5a | Remove the antenna or use a dead SIM, then run `check`. | the LED blinks slowly (message waiting); `status` shows it queued |
| 5b | Restore the signal. | the message arrives within 5 minutes, once (no duplicate) |
| 5c | Power off while a message is queued, then power on. | "message(s) waiting from before the restart" is printed and it is sent |

## 6. Usability, with 3–5 older relatives (informal; this is not a clinical study and no health data is recorded)

| Step | How | Pass |
| --- | --- | --- |
| 6a | Explain once, then let them do a full check alone. Stopwatch from pressing green to the result. | under 2 minutes; record the time per language (design estimate 110–116 s, `voice/timing_budget.py`) |
| 6b | Did they understand all three questions? Could they press green/red with the left hand? | yes, without help |
| 6c | Could they hear the voice with a TV on? | yes; if not, use a larger speaker |

## Results table (fill in)

| Test | Date | Who | Result | Pass? | Notes |
| --- | --- | --- | --- | --- | --- |
| 1a | | | | | |
| 1c | | | | | |
| 2a | | | | | |
| 2b | | | | | |
| 3b | | | | | |
| 3d | | | | | |
| 3g | | | | | |
| 4a | | | | | |
| 4c | | | | | |
| 5b | | | | | |
| 6a | | | | | |
