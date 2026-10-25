// SAATHI chair firmware - check-quality rules (pure logic, no hardware; also compiled on a PC by host_test/).
// Decides which readings of a finished check can be trusted, so a failed sensor is never stored as a normal reading.
#pragma once
#include <Arduino.h>
#include "dsp.h"
#include "detector.h"

enum CheckFlag : uint8_t {
  FLAG_FINGER_FAILED   = 1,   // finger sensor gave no pulse (heart rate may still come from the seat film)
  FLAG_FOOT_CONTACT    = 2,   // foot plates found no contact although the test was allowed
  FLAG_WEIGHT_REJECTED = 4,   // weight not used: feet were not confirmed on the footrest, so feet may be off the chair
  FLAG_WEIGHT_UNVERIFIED = 8, // foot contact could not be checked (pacemaker/ICD or charger): weight used but marked
};

// fingerFailed: judged on the finger sensor's OWN readings, not on the fused heart rate (the seat film can hide it)
bool fingerFailed(const CheckResult& r);

// Copies a finished check into today's record, applying the rules above. Returns the CheckFlag bits.
// zAllowed = the impedance test was allowed to run (no implant, no charger).
uint8_t applyCheck(DayRecord& rec, const CheckResult& r, float footZ, bool zAllowed);
