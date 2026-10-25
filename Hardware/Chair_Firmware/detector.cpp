// SAATHI chair firmware - detector (port of detector.py: score_patient + apply_threshold).
#include "detector.h"
#include "config.h"
#include "dsp.h"
#include <math.h>

namespace {

enum Kind { ABS, REL };
struct Signal {
  const char* label; int dir; float weight; float floor; Kind kind;
  float DayRecord::* field;
};

// direction +1 = worse when higher, -1 = worse when lower, 0 = either way (PTT, secondary)
const Signal SIGNALS[] = {
  {"Weight",              +1, 1.00f, 0.40f, ABS, &DayRecord::weight},   // weight is time-of-day adjusted first
  {"Foot impedance",      -1, 1.00f, 0.02f, REL, &DayRecord::footZ},
  {"Resting heart rate",  +1, 0.70f, 3.0f,  ABS, &DayRecord::hr},
  {"Breathing rate",      +1, 0.70f, 2.0f,  ABS, &DayRecord::rr},
  {"Oxygen (SpO2)",       -1, 0.50f, 1.0f,  ABS, &DayRecord::spo2},
  {"Pulse transit time",   0, 0.25f, 8.0f,  ABS, &DayRecord::ptt},
};

float refHour(const DayRecord* h, int n) {
  float v[BASELINE_DAYS]; int m = 0;
  for (int i = 0; i < n && m < BASELINE_DAYS; i++)
    if (h[i].day < BASELINE_DAYS && h[i].measured && !isnan(h[i].hour)) v[m++] = h[i].hour;
  return m ? medianOf(v, m) : 9.0f;
}

float valueOf(const DayRecord& r, const Signal& s, float slope, float ref) {
  float v = r.*(s.field);
  if (s.field == &DayRecord::weight && !isnan(v) && !isnan(r.hour)) v -= slope * (r.hour - ref);
  return v;
}

}  // namespace

Verdict evaluateToday(DayRecord* h, int n) {
  Verdict out;
  DayRecord& t = h[n - 1];
  if (t.day < BASELINE_DAYS) { t.score = NAN; return out; }
  if (!t.measured) {
    // checks stopped right after a high score: the 2nd missed day in a row raises an alert (missing data is never "safe")
    t.score = NAN;
    bool overdue = n >= 2 && !h[n - 2].measured && h[n - 2].day == t.day - 1;
    const DayRecord* last = nullptr; bool firedSince = false;
    for (int i = n - 2; i >= 0; i--) {
      if (!isnan(h[i].score)) { last = &h[i]; break; }
      if (h[i].alert) firedSince = true;
    }
    if (overdue && last && last->day >= BASELINE_DAYS && last->highScore && !firedSince) {
      strcpy(out.rule, "high score, then checks stopped");
      snprintf(out.reason, sizeof(out.reason), "Last check (day %d) scored above normal; no check for 2 days", last->day);
      out.alert = true;
    }
    t.alert = out.alert;
    return out;
  }
  const float ref = refHour(h, n);

  struct Part { float pts; char text[48]; };
  Part parts[10]; int np = 0;
  float pool[MAX_DAYS];
  float spo2Med = NAN;   // this patient's normal SpO2 (for the relative danger rule)

  // identity check: weight more than OTHER_PERSON_KG from this patient's normal. It may be someone else, or it may be
  // this patient with a large real change, so nothing is suppressed: unless the patient confirmed it is them, the
  // weight is left out of today's score and the day is not learned, but all other signals and rules still run.
  float wDev = NAN;
  {
    int m = 0;
    for (int i = 0; i < n - 1; i++) {
      const DayRecord& r = h[i];
      bool inPool = r.day < BASELINE_DAYS || (r.day <= t.day - BASE_LAG && !r.excluded);
      float x = valueOf(r, SIGNALS[0], WEIGHT_KG_PER_H, ref);
      if (inPool && r.measured && !isnan(x)) pool[m++] = x;
    }
    float w = valueOf(t, SIGNALS[0], WEIGHT_KG_PER_H, ref);
    if (m >= 5 && !isnan(w)) {
      int mr = m > BASE_WINDOW ? BASE_WINDOW : m;
      float tmp[MAX_DAYS]; memcpy(tmp, pool + (m - mr), mr * sizeof(float));
      wDev = w - medianOf(tmp, mr);
    }
  }
  t.other = (!isnan(wDev) && fabsf(wDev) > OTHER_PERSON_KG && t.identity != 1) ? 1 : 0;
  t.incomplete = (int)!isnan(t.weight) + (int)!isnan(t.hr) + (int)!isnan(t.rr) + (int)!isnan(t.spo2) < 2;

  for (const Signal& s : SIGNALS) {
    if (t.other && s.field == &DayRecord::weight) continue;    // identity uncertain: weight left out, the rest still scored
    float v = valueOf(t, s, WEIGHT_KG_PER_H, ref);
    if (isnan(v)) continue;
    // baseline pool: first 14 days + days at least BASE_LAG old that were not excluded; most recent 28
    int m = 0;
    for (int i = 0; i < n - 1; i++) {
      const DayRecord& r = h[i];
      bool inPool = r.day < BASELINE_DAYS || (r.day <= t.day - BASE_LAG && !r.excluded);
      float x = valueOf(r, s, WEIGHT_KG_PER_H, ref);
      if (inPool && r.measured && !isnan(x)) pool[m++] = x;
    }
    if (m < 5) continue;
    float* recent = pool + (m > BASE_WINDOW ? m - BASE_WINDOW : 0);
    int mr = m > BASE_WINDOW ? BASE_WINDOW : m;
    float tmp[MAX_DAYS]; memcpy(tmp, recent, mr * sizeof(float));
    float med = medianOf(tmp, mr);
    float spread = madOf(recent, mr, med);
    if (s.field == &DayRecord::spo2) spo2Med = med;
    float fl = s.kind == REL ? s.floor * fabsf(med) : s.floor;
    if (isnan(spread) || spread < fl) spread = fl;
    float z = (v - med) / spread;
    z = s.dir == 0 ? fabsf(z) : s.dir * z;
    float pts = constrain(z - 1.0f, 0.0f, 3.0f) * s.weight;
    if (pts > 0 && np < 10) {
      float d = v - med;
      Part& p = parts[np++]; p.pts = pts;
      if (s.field == &DayRecord::footZ) snprintf(p.text, sizeof(p.text), "Foot impedance %+.0f%% vs usual", 100 * d / med);
      else if (s.field == &DayRecord::weight) snprintf(p.text, sizeof(p.text), "Weight %+.1f kg vs usual", d);
      else if (s.field == &DayRecord::spo2) snprintf(p.text, sizeof(p.text), "Oxygen (SpO2) %+.1f points vs usual", d);
      else if (s.field == &DayRecord::ptt) snprintf(p.text, sizeof(p.text), "Pulse transit time %+.0f ms vs usual", d);
      else if (s.field == &DayRecord::hr) snprintf(p.text, sizeof(p.text), "Resting heart rate %+.0f bpm vs usual", d);
      else snprintf(p.text, sizeof(p.text), "Breathing rate %+.0f /min vs usual", d);
    }
  }
  // activity (sit-downs), relative, worse when lower
  {
    float pool2[MAX_DAYS]; int m = 0;
    for (int i = 0; i < n - 1; i++) {
      const DayRecord& r = h[i];
      bool inPool = r.day < BASELINE_DAYS || (r.day <= t.day - BASE_LAG && !r.excluded);
      if (inPool) pool2[m++] = r.sits;
    }
    if (m >= 5) {
      float* recent = pool2 + (m > BASE_WINDOW ? m - BASE_WINDOW : 0);
      int mr = m > BASE_WINDOW ? BASE_WINDOW : m;
      float tmp[MAX_DAYS]; memcpy(tmp, recent, mr * sizeof(float));
      float med = medianOf(tmp, mr), spread = madOf(recent, mr, med), fl = 0.20f * fabsf(med);
      if (isnan(spread) || spread < fl) spread = fl;
      if (spread > 0) {
        float pts = constrain(-(t.sits - med) / spread - 1.0f, 0.0f, 3.0f) * 0.25f;
        if (pts > 0 && np < 10) { parts[np].pts = pts; snprintf(parts[np].text, 48, "Chair use (sit-downs) %+.0f%% vs usual", 100 * (t.sits - med) / med); np++; }
      }
    }
  }
  // symptom answers
  const struct { int8_t DayRecord::* f; const char* txt; } Q[] = {
    {&DayRecord::qOrtho, "reported needing extra pillows to breathe"},
    {&DayRecord::qBreath, "reported breathlessness"},
    {&DayRecord::qSwell, "reported more foot swelling"}};
  for (auto& q : Q) if (t.*(q.f) == 1 && np < 10) { parts[np].pts = Q_WEIGHT; strncpy(parts[np].text, q.txt, 47); parts[np].text[47] = 0; np++; }

  float score = 0;
  for (int i = 0; i < np; i++) score += parts[i].pts;
  t.excluded = t.other || score >= BASE_EXCLUDE;     // baseline exclusion uses the unrounded score (as detector.py)
  score = roundf(score * 100.0f) / 100.0f;          // alert threshold uses 2 decimals, exactly as detector.py stores it
  t.score = score;
  t.highScore = score >= ALERT_THRESHOLD;
  // SpO2 danger is relative to the person's own normal: a hobby sensor is not calibrated for absolute values
  t.spo2Low = !isnan(t.spo2) && !isnan(spo2Med) && t.spo2 <= spo2Med - SPO2_DROP;

  // previous scored day (missed, learning and other-person days do not break or count toward "2 days in a row")
  const DayRecord* prev = nullptr;
  for (int i = n - 2; i >= 0; i--) if (h[i].day >= BASELINE_DAYS && !isnan(h[i].score)) { prev = &h[i]; break; }

  if (t.highScore && prev && prev->highScore) strcpy(out.rule, "score high 2 days in a row");
  // danger 1: weight +2 kg within 3 days (time-of-day adjusted)
  if (!out.rule[0] && !t.other) {
    float wNow = valueOf(t, SIGNALS[0], WEIGHT_KG_PER_H, ref), wMin = NAN;
    for (int i = 0; i < n - 1; i++) if (h[i].day >= t.day - 3 && h[i].day < t.day && h[i].measured && !h[i].other) {
      float w = valueOf(h[i], SIGNALS[0], WEIGHT_KG_PER_H, ref);
      if (!isnan(w) && (isnan(wMin) || w < wMin)) wMin = w;
    }
    if (!isnan(wNow) && !isnan(wMin) && wNow - wMin >= DANGER_KG_3D) strcpy(out.rule, "danger: weight +2 kg in 3 days");
  }
  if (!out.rule[0] && t.spo2Low && prev && prev->spo2Low) strcpy(out.rule, "danger: SpO2 4+ points below usual two days running");

  // reason: top 4 contributions
  for (int a = 0; a < np; a++) for (int b = a + 1; b < np; b++) if (parts[b].pts > parts[a].pts) { Part x = parts[a]; parts[a] = parts[b]; parts[b] = x; }
  out.reason[0] = 0;
  for (int i = 0; i < np && i < 4; i++) {
    if (i) strncat(out.reason, "; ", sizeof(out.reason) - strlen(out.reason) - 1);
    strncat(out.reason, parts[i].text, sizeof(out.reason) - strlen(out.reason) - 1);
  }
  if (t.other) {
    char tmp[sizeof(out.reason)];
    snprintf(tmp, sizeof(tmp), "Weight %+.1f kg from usual: please confirm who sat (weight not used today)%s%s",
             wDev, out.reason[0] ? "; " : "", out.reason);
    strncpy(out.reason, tmp, sizeof(out.reason) - 1); out.reason[sizeof(out.reason) - 1] = 0;
  }
  if (out.reason[0] >= 'a' && out.reason[0] <= 'z') out.reason[0] -= 32;
  out.score = score;
  out.alert = out.rule[0] != 0;
  t.alert = out.alert;
  return out;
}

const char* statusWord(const DayRecord& t) {
  if (t.day < BASELINE_DAYS) return "LEARNING";
  if (!t.measured) return "MISSED";
  if (t.alert) return "ALERT";
  if (t.other) return "CONFIRM WHO SAT";
  if (t.incomplete) return "INCOMPLETE";
  if (!isnan(t.score) && t.score >= ALERT_THRESHOLD) return "WATCH";
  return "NORMAL";
}
