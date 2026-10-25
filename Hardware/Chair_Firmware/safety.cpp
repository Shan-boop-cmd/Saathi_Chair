// SAATHI chair firmware - check-quality rules. See safety.h. Tested by host_test/test_safety.cpp.
#include "safety.h"
#include <math.h>

bool fingerFailed(const CheckResult& r) { return isnan(r.hrPpg) || isnan(r.spo2); }

uint8_t applyCheck(DayRecord& rec, const CheckResult& r, float footZ, bool zAllowed) {
  uint8_t f = 0;
  if (fingerFailed(r)) f |= FLAG_FINGER_FAILED;
  rec.weightRaw = r.weightKg;
  rec.weight = r.weightKg;
  if (zAllowed && isnan(footZ)) {
    f |= FLAG_FOOT_CONTACT;
    if (!isnan(r.weightKg)) { f |= FLAG_WEIGHT_REJECTED; rec.weight = NAN; }   // feet may be on the floor: not a trustworthy weight
  } else if (!zAllowed && !isnan(r.weightKg)) {
    f |= FLAG_WEIGHT_UNVERIFIED;
  }
  rec.footZ = footZ;
  rec.hr = r.hr;
  rec.spo2 = r.spo2;                 // NAN when the finger sensor failed: shown as "unavailable", never as normal
  rec.rr = r.rr;
  rec.ptt = r.pttMs;
  rec.flags = f;
  return f;
}
