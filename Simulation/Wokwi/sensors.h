// SAATHI chair firmware - sensors: load cells, seat BCG, finger PPG, foot impedance, buttons.
#pragma once
#include <Arduino.h>
#include "dsp.h"

bool sensorsBegin();                 // returns false if the MAX30102 is missing (check still runs on BCG)
float readWeightKg();                // quick reading (load cells), NAN if not ready
bool chargerPlugged();
float batteryVolts();

// Runs the 90-second measurement, returns everything pipeline-equivalent.
// keepGoing() is polled often; return false to abort (e.g. SOS pressed).
void measureCheck(CheckResult& out, bool (*keepGoing)());

// Foot impedance (ohm) by the ratio method; NAN if blocked (charger, implant) or poor contact.
float measureFootImpedance(bool implant);

// Load-cell calibration helpers (serial console)
void tareChair();
void autoZero();                     // re-zero the empty chair after 10 quiet minutes (slow drift only)
void calibrateWeight(float knownKg);

// Buttons (debounced). Returns true once per press.
bool greenPressed();
bool redPressed();
bool sosPressed();
void ledSet(bool on);
