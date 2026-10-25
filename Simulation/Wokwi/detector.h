// SAATHI chair firmware - early-warning detector. Port of detector.py (same constants, same rules).
#pragma once
#include <Arduino.h>

struct DayRecord {
  int16_t day = -1;
  uint8_t measured = 0;
  float hour = NAN, weight = NAN, footZ = NAN, hr = NAN, spo2 = NAN, rr = NAN, ptt = NAN;
  int16_t sits = 0;
  int8_t qOrtho = -1, qBreath = -1, qSwell = -1;   // -1 = no answer
  float score = NAN;
  uint8_t alert = 0, excluded = 0, spo2Low = 0, highScore = 0;
  uint8_t other = 0;        // identity uncertain: weight > OTHER_PERSON_KG from normal and not confirmed by the patient.
                            // Weight is left out of the score and the day is not learned, but every other safety check runs.
  uint8_t identity = 0;     // 0 not asked, 1 patient confirmed "yes, this is me" (weight then counts in full), 2 no answer
  uint8_t flags = 0;        // CheckFlag bits from safety.h (finger failed, foot contact failed, weight rejected...)
  float weightRaw = NAN;    // the load-cell reading before any rejection (kept for the doctor's view)
  uint8_t incomplete = 0;   // fewer than 2 core signals (weight, HR, breathing, SpO2) measured
};

struct Verdict {
  float score = NAN;
  bool alert = false;
  char rule[64] = "";
  char reason[200] = "";
};

// history: all stored days in order (oldest first), today = last element, already filled with today's readings.
// Fills today's score/alert/excluded flags and returns the verdict with a plain-language reason.
Verdict evaluateToday(DayRecord* history, int n);

// status word for the screen / SMS: LEARNING, NORMAL, WATCH, ALERT, MISSED, INCOMPLETE, OTHER PERSON.
// Missing or doubtful data never shows as NORMAL.
const char* statusWord(const DayRecord& today);
