// SAATHI chair firmware - main program.
//   Idle: an ordinary chair. Counts sit-downs, reminds in the evening if no check was done, SOS works any time.
//   Green pressed: position prompt -> foot impedance (also confirms bare feet are on the footrest) -> 90-second
//   measurement, with the 3 spoken questions asked DURING it (answered with the left hand: green/red on the left
//   armrest) -> score against this person's normal -> spoken result -> SMS to the family (queued, retried).
//   Designed and timed to stay under 2 minutes in all three languages (see voice/timing_budget.py).
// Serial console (115200 baud): type 'help'.
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <sys/time.h>
#include "config.h"
#include "dsp.h"
#include "detector.h"
#include "sensors.h"
#include "storage.h"
#include "voice.h"
#include "sms.h"
#include "safety.h"
#include <esp_sleep.h>
#include <driver/gpio.h>

static bool clockOk = false;
static int pendingSits = 0;          // sit-downs counted before today's record exists
static bool wasSitting = false, remindedThisSit = false, sosFlag = false;
static int lastMissedSmsDay = -1;
static int lastBatteryWarnDay = -1;
static uint32_t lastActivity = 0;             // last button / serial / sitting activity (for light sleep)
constexpr uint32_t QUESTION_DELAY_MS = 20000;  // questions start 20 s into the measurement (after settling)
constexpr float    BATTERY_LOW_V = 3.45f;
constexpr bool     LOW_POWER_IDLE = !SAATHI_DEMO;   // light sleep between load-cell polls when idle

// ---------------- time and day index ----------------
static bool timeValid() { return time(nullptr) > 1700000000; }

static void syncClock() {
  uint32_t ep;
  if (networkTime(&ep)) { timeval tv{(time_t)ep, 0}; settimeofday(&tv, nullptr); }
  clockOk = timeValid();
  if (clockOk && settings().setupEpoch == 0) {
    time_t now = time(nullptr);
    settings().setupEpoch = (uint32_t)(now - now % 86400);   // start of the first day
    saveSettings();
  }
}

static int currentDay() {
#if SAATHI_DEMO
  return historyCount();                     // demo: every check is a new day
#else
  if (clockOk) return (int)((time(nullptr) - (time_t)settings().setupEpoch) / 86400);
  int n = historyCount();                    // no clock: one day per completed check
  return n ? history()[n - 1].day + (history()[n - 1].measured ? 1 : 0) : 0;
#endif
}

static float currentHour() {
  if (!clockOk) return NAN;
  time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
  return t.tm_hour + t.tm_min / 60.0f;
}

// ---------------- helpers ----------------
// The 3 questions run as a small state machine driven from the measurement loop, so they are asked while the
// 90-second measurement continues. Answers: GREEN = yes (1), RED = no (0), no answer after a reminder = -1.
struct QState { int idx = 0, attempt = 0; bool speaking = false; uint32_t askedAt = 0, startAt = 0; int8_t ans[3] = {-1, -1, -1}; bool done = false; };
static QState qs;
static const Prompt QP[3] = {P_Q_ORTHO, P_Q_BREATH, P_Q_SWELL};

static void nextQuestion() { qs.idx++; qs.attempt = 0; qs.askedAt = 0; qs.speaking = false; if (qs.idx >= 3) qs.done = true; }

static void questionsStep() {
  if (qs.done || (int32_t)(millis() - qs.startAt) < 0) return;
  if (qs.speaking || qs.askedAt) {                      // an answer is accepted while the question is still playing
    if (greenPressed()) { sayStop(); qs.ans[qs.idx] = 1; nextQuestion(); return; }
    if (redPressed())   { sayStop(); qs.ans[qs.idx] = 0; nextQuestion(); return; }
  }
  if (qs.speaking) { if (sayService()) return; qs.speaking = false; qs.askedAt = millis(); return; }
  if (!qs.askedAt) { qs.speaking = sayStart(qs.attempt ? P_NO_ANSWER : QP[qs.idx]); if (!qs.speaking) qs.askedAt = millis(); return; }
  if (millis() - qs.askedAt > ANSWER_TIMEOUT_MS) {
    if (qs.attempt == 0) { qs.attempt = 1; qs.askedAt = 0; } else { qs.ans[qs.idx] = -1; nextQuestion(); }
  }
}

static bool keepGoing() {
  if (sosPressed()) { sosFlag = true; }
  questionsStep();
  return true;                               // the check continues; SOS is handled right after
}

static void handleSos() {
  char msg[160];
  snprintf(msg, sizeof(msg), "SAATHI SOS %s: the SOS button was pressed on the chair. Please call or visit now.", settings().patientId);
  smsSend(settings().caregiver, msg);
  say(P_SOS_SENT);
  sosFlag = false;
}

static void uploadDay(const DayRecord& r, const Verdict& v, float checkSeconds) {
  if (!settings().wifiSsid[0] || !settings().uploadUrl[0]) return;
  WiFi.begin(settings().wifiSsid, settings().wifiPass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(200);
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(settings().uploadUrl);
    http.addHeader("Content-Type", "application/json");
    char body[512];
    snprintf(body, sizeof(body),
             "{\"patient_id\":\"%s\",\"day\":%d,\"check_s\":%.0f,\"status\":\"%s\",\"test_hour\":%.2f,\"weight_kg\":%.1f,\"foot_z_ohm\":%.0f,\"rest_hr_bpm\":%.1f,"
             "\"spo2_pct\":%.1f,\"resp_rate_bpm\":%.1f,\"ptt_ms\":%.0f,\"sit_count\":%d,\"q\":[%d,%d,%d],\"score\":%.2f,\"alert\":%d,"
             "\"reason\":\"%s\"}",
             settings().patientId, r.day, checkSeconds, statusWord(r), r.hour, r.weight, r.footZ, r.hr, r.spo2, r.rr, r.ptt, r.sits, r.qOrtho, r.qBreath,
             r.qSwell, isnan(r.score) ? -1.0f : r.score, r.alert, v.reason);
    int code = http.POST(body);
    Serial.printf("[UPLOAD] HTTP %d\n", code);
    http.end();
  }
  WiFi.disconnect(true); WiFi.mode(WIFI_OFF);   // radio off again: keeps ADC1 clean and saves battery
}

// ---------------- the daily check ----------------
// one spoken yes/no question outside the measurement: GREEN = 1, RED = 0, no answer after a reminder = -1
static int8_t askYesNo(Prompt p) {
  for (int attempt = 0; attempt < 2; attempt++) {
    say(attempt ? P_NO_ANSWER : p);
    uint32_t t0 = millis();
    while (millis() - t0 < ANSWER_TIMEOUT_MS) {
      if (greenPressed()) return 1;
      if (redPressed()) return 0;
      if (sosPressed()) sosFlag = true;
      delay(10);
    }
  }
  return -1;
}

static void runCheck() {
  int day = currentDay();
  DayRecord& rec = todayRecord(day);
  rec.sits += pendingSits; pendingSits = 0;

  ledSet(true);
  const uint32_t t0 = millis();
  say(P_POSITION);
  // foot impedance first: it also confirms bare feet are on the footrest (so the feet are weighed too)
  bool zAllowed = !settings().implant && !chargerPlugged();
  float z = measureFootImpedance(settings().implant);
  if (zAllowed && isnan(z)) { say(P_POSITION); z = measureFootImpedance(settings().implant); }
  const uint32_t t1 = millis();
  say(P_HOLD);
  qs = QState(); qs.startAt = millis() + QUESTION_DELAY_MS;
  CheckResult r;
  measureCheck(r, keepGoing);                            // the questions are asked inside this 90 s
  const uint32_t t2 = millis();
  while (!qs.done) { if (sosPressed()) sosFlag = true; questionsStep(); }   // only if answers came late
  const uint32_t t3 = millis();
  if (sosFlag) handleSos();
  rec.qOrtho = qs.ans[0]; rec.qBreath = qs.ans[1]; rec.qSwell = qs.ans[2];
  rec.measured = 1;
  rec.hour = currentHour();
  rec.identity = 0;
  // check-quality rules (safety.cpp): the finger sensor is judged on its own readings, and a weight taken while the
  // foot plates found no contact is rejected, because the feet may have been on the floor
  const uint8_t flags = applyCheck(rec, r, z, zAllowed);
  if (flags & FLAG_FINGER_FAILED) say(P_FINGER);        // SpO2 and breathing are stored as unavailable, never as normal
  Verdict v = evaluateToday(history(), historyCount());
  // large weight change: ask, never suppress. Yes -> weight counts in full. No -> someone else: nothing saved.
  // No answer -> scored without weight, all other safety checks still run, family told to check.
  bool discarded = false;
  if (rec.other) {
    int8_t yes = askYesNo(P_CONFIRM);
    if (yes == 1) { rec.identity = 1; v = evaluateToday(history(), historyCount()); }
    else if (yes == 0) {
      int d = rec.day, sits = rec.sits;
      rec = DayRecord(); rec.day = d; rec.sits = sits;  // not this patient: today's check is not saved
      discarded = true;
    } else rec.identity = 2;
  }
  const char* st = statusWord(rec);

  if (discarded) say(P_OTHER);
  else if (rec.day < BASELINE_DAYS) say(P_LEARNING);
  else if (v.alert) say(P_ALERT);
  else if (rec.incomplete) say(P_INCOMPLETE);
  else if (strcmp(st, "WATCH") == 0) say(P_DONE_WATCH);
  else say(P_DONE_NORMAL);
  const uint32_t t4 = millis();
  const float checkSeconds = (t4 - t0) / 1000.0f;
  Serial.printf("[TIMING] position + foot check %.1f s | hold + measurement + questions %.1f s | late answers %.1f s | result %.1f s | TOTAL %.1f s\n",
                (t1 - t0) / 1000.0f, (t2 - t1) / 1000.0f, (t3 - t2) / 1000.0f, (t4 - t3) / 1000.0f, checkSeconds);

  char msg[360], notes[140] = "";
  if (flags & FLAG_FINGER_FAILED) strncat(notes, " Finger sensor gave no reading (SpO2 unavailable).", sizeof(notes) - strlen(notes) - 1);
  if (flags & FLAG_WEIGHT_REJECTED) strncat(notes, " Weight not used: feet were not on the plates.", sizeof(notes) - strlen(notes) - 1);
  if (rec.other) strncat(notes, " Weight very different from usual and not confirmed: please check who did the check.", sizeof(notes) - strlen(notes) - 1);
  auto num = [](float x, char* buf, const char* fmt) { if (isnan(x)) strcpy(buf, "unavailable"); else snprintf(buf, 16, fmt, x); return buf; };
  char w[16], hr[16], sp[16], rr[16];
  if (discarded) {
    msg[0] = 0;                                          // someone else sat: nothing saved, nothing sent
  } else if (v.alert) {
    snprintf(msg, sizeof(msg), "SAATHI ALERT %s: readings above their normal (%s). %s.%s Please call the doctor today. Monitoring aid, not a diagnosis.",
             settings().patientId, v.rule, v.reason, notes);
  } else {
    snprintf(msg, sizeof(msg), "SAATHI %s day %d: check done. Status %s. Weight %s kg, HR %s, SpO2 %s, breathing %s/min.%s",
             settings().patientId, rec.day, st, num(rec.weight, w, "%.1f"), num(rec.hr, hr, "%.0f"), num(rec.spo2, sp, "%.0f%%"),
             num(rec.rr, rr, "%.0f"), notes);
  }
  if (msg[0]) smsSend(settings().caregiver, msg);
  saveHistory();
  ledSet(false);

  Serial.printf("[CHECK] day %d  weight %.1f kg  foot |Z| %.0f ohm  HR %.1f (%s, BCG %.1f / PPG %.1f)  SpO2 %.1f  breathing %.1f  PTT %.0f ms  answers %d/%d/%d  score %.2f  %s %s\n",
                rec.day, rec.weight, rec.footZ, rec.hr, r.hrSource, r.hrBcg, r.hrPpg, rec.spo2, rec.rr, rec.ptt,
                rec.qOrtho, rec.qBreath, rec.qSwell, rec.score, st, v.alert ? v.reason : "");
  uploadDay(rec, v, checkSeconds);
}

// ---------------- demo patient (SAATHI_DEMO) ----------------
static void demoAdvance() {
#if SAATHI_DEMO
  DemoState& s = demoState();
  int d = historyCount();
  s.trueWeight = 64.0f; s.trueHr = 72.0f; s.trueSpo2 = 96.0f; s.trueRr = 15.0f; s.trueZ = 520.0f;
  if (d >= 20) {                                 // worsening from day 20: fluid builds over 6 days
    float k = min(d - 19, 6);
    s.trueWeight += 0.35f * k; s.trueHr += 2.0f * k; s.trueRr += 0.8f * k; s.trueSpo2 -= 0.35f * k; s.trueZ *= 1 - 0.012f * k;
  }
#endif
}

// ---------------- serial console ----------------
static void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n'); line.trim();
  if (!line.length()) return;
  String cmd = line, arg;
  int sp = line.indexOf(' ');
  if (sp > 0) { cmd = line.substring(0, sp); arg = line.substring(sp + 1); arg.trim(); }
  if (cmd == "help") {
    Serial.println("num <+91...> | lang <en|hi|ta> | id <name> | implant <0|1> | tare | cal <kg> | zcal <ohm> | wifi <ssid> <pass> | url <http://...>");
    Serial.println("check | sos | dump | erase | status | demo-days <n> (demo build only)");
    Serial.println("bench tests (hardware/BENCH_TESTS.md): weigh | zread | bcg");
  } else if (cmd == "num") { strncpy(settings().caregiver, arg.c_str(), sizeof(settings().caregiver) - 1); saveSettings(); }
  else if (cmd == "lang") { strncpy(settings().lang, arg.c_str(), 2); settings().lang[2] = 0; saveSettings(); }
  else if (cmd == "id") { strncpy(settings().patientId, arg.c_str(), sizeof(settings().patientId) - 1); saveSettings(); }
  else if (cmd == "implant") { settings().implant = arg.toInt() == 1; saveSettings(); }
  else if (cmd == "tare") { tareChair(); Serial.println("empty chair set to 0 kg"); }
  else if (cmd == "cal") { calibrateWeight(arg.toFloat()); Serial.printf("scale %.1f counts/kg\n", settings().hxScale); }
  else if (cmd == "zcal") {
    // put a known resistor (e.g. 500 ohm) across the plates first; one-time factor
    float raw = measureFootImpedance(false) / settings().zCal;
    if (!isnan(raw) && raw > 0) { settings().zCal = arg.toFloat() / raw; saveSettings(); }
    Serial.printf("impedance factor %.4f\n", settings().zCal);
  }
  else if (cmd == "wifi") { int s2 = arg.indexOf(' '); strncpy(settings().wifiSsid, arg.substring(0, s2).c_str(), 32); strncpy(settings().wifiPass, arg.substring(s2 + 1).c_str(), 64); saveSettings(); }
  else if (cmd == "url") { strncpy(settings().uploadUrl, arg.c_str(), sizeof(settings().uploadUrl) - 1); saveSettings(); }
  else if (cmd == "check") { demoAdvance(); runCheck(); }
  // ---- bench tests: print raw readings for comparison with a reference scale / resistor / oscilloscope ----
  else if (cmd == "weigh") {                 // 20 readings, 0.5 s apart
    for (int i = 0; i < 20; i++) { Serial.printf("weight %.2f kg\n", readWeightKg()); delay(500); }
  }
  else if (cmd == "zread") {                 // 5 impedance readings (needs battery power, no implant flag)
    for (int i = 0; i < 5; i++) Serial.printf("foot |Z| %.1f ohm\n", measureFootImpedance(settings().implant));
  }
  else if (cmd == "bcg") {                   // 10 s of the seat front-end at the ADC: size of the heartbeat signal
    uint16_t lo = 4095, hi = 0; uint32_t t0 = millis(); uint32_t n = 0;
    while (millis() - t0 < 10000) { uint16_t v = analogReadMilliVolts(PIN_BCG_ADC); lo = min(lo, v); hi = max(hi, v); n++; delay(2); }
    Serial.printf("BCG at ADC: min %u mV, max %u mV, swing %u mV over %lu samples (aim 300-2500 mV; see BENCH_TESTS.md)\n",
                  lo, hi, hi - lo, (unsigned long)n);
  }
  else if (cmd == "sos") handleSos();
  else if (cmd == "dump") printHistoryCsv(Serial);
  else if (cmd == "erase") { eraseHistory(); Serial.println("history erased"); }
  else if (cmd == "format-storage") {
    if (arg == "YES") Serial.println(formatStorage() ? "storage formatted" : "format FAILED");
    else Serial.println("this erases all history and queued messages: type 'format-storage YES'");
  }
  else if (cmd == "status") {
    Serial.printf("patient %s  caregiver %s  lang %s  implant %d  days %d  clock %s  battery %.2f V  charger %d\n", settings().patientId,
                  settings().caregiver, settings().lang, settings().implant, historyCount(), clockOk ? "ok" : "none", batteryVolts(), chargerPlugged());
  }
#if SAATHI_DEMO
  else if (cmd == "demo-days") {
    // fast-forward: run n checks answering 'no' automatically (answers left empty)
    int n = arg.toInt();
    for (int i = 0; i < n; i++) {
      demoAdvance();
      int day = currentDay(); DayRecord& rec = todayRecord(day);
      CheckResult r; measureCheck(r, keepGoing);
      rec.measured = 1; rec.hour = 8.5f; rec.weight = r.weightKg; rec.hr = r.hr; rec.spo2 = r.spo2; rec.rr = r.rr; rec.ptt = r.pttMs;
      rec.footZ = measureFootImpedance(false); rec.sits = 8 + random(0, 4); rec.qOrtho = rec.qBreath = rec.qSwell = 0;
      Verdict v = evaluateToday(history(), historyCount());
      Serial.printf("day %2d  weight %.1f  HR %.0f  SpO2 %.1f  RR %.0f  |Z| %.0f  score %5.2f  %-8s %s\n", rec.day, rec.weight, rec.hr, rec.spo2,
                    rec.rr, rec.footZ, isnan(rec.score) ? 0 : rec.score, statusWord(rec), v.alert ? v.reason : "");
    }
    saveHistory();
  }
#endif
  else Serial.println("unknown command; type help");
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nSAATHI chair, built %s%s\n", __DATE__, SAATHI_DEMO ? " (DEMO build: simulated sensors)" : "");
  if (!storageBegin())
    Serial.println("STORAGE ERROR: flash could not be mounted. Running from RAM: readings are NOT saved. "
                   "Nothing was erased. Type 'format-storage YES' only if you accept losing the stored history.");
  loadSettings();
  bool ppg = sensorsBegin();
  voiceBegin();
  smsBegin();
  syncClock();
  WiFi.mode(WIFI_OFF);
  if (!storageOk()) smsSend(settings().caregiver, "SAATHI: the chair's memory has a fault, so readings are not being saved. Please contact the team.");
  Serial.printf("finger sensor %s, %d days stored, clock %s. Type 'help'.\n", ppg ? "ok" : "MISSING", historyCount(), clockOk ? "ok" : "none");
#if SAATHI_DEMO
  Serial.println("DEMO: press GREEN for a check (each check = one day). Worsening starts on day 20. 'demo-days 19' fast-forwards.");
#endif
}

// Light sleep between load-cell polls when nobody is using the chair: ~7 mA instead of ~90 mA from the battery
// (hardware/power_budget.py), so a power cut of about 3 weeks is covered instead of under 2 days.
// Buttons and the serial console wake it immediately; the 200 ms timer keeps sit-down counting working.
static void idleSleep(bool sitting) {
  if (!LOW_POWER_IDLE || sitting || millis() - lastActivity < 30000 || Serial.available()) return;
  Serial.flush();
  for (int pin : {PIN_BTN_GREEN, PIN_BTN_RED, PIN_BTN_SOS}) gpio_wakeup_enable((gpio_num_t)pin, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(200000);
  esp_light_sleep_start();
}

void loop() {
  handleSerial();
  if (Serial.available()) lastActivity = millis();
  if (sosPressed() || sosFlag) { lastActivity = millis(); handleSos(); }
  smsService();
  bool sittingNow = wasSitting;

  // occupancy and evening reminder
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll > 500) {
    lastPoll = millis();
    float w = readWeightKg();
    bool sitting = !isnan(w) && w > SIT_KG;
    sittingNow = sitting;
    if (sitting) lastActivity = millis();
    static uint32_t emptySince = 0;                      // slow drift of the empty chair: re-zero after 10 quiet minutes
    if (!sitting && !isnan(w) && fabsf(w) < 0.3f) {
      if (!emptySince) emptySince = millis();
      if (millis() - emptySince > 600000UL) { autoZero(); emptySince = millis(); }
    } else emptySince = 0;
    if (sitting && !wasSitting) {
      pendingSits++;
      remindedThisSit = false;
      int day = currentDay(), n = historyCount();
      bool doneToday = n && history()[n - 1].day == day && history()[n - 1].measured;
      float h = currentHour();
      if (!doneToday && !isnan(h) && h >= EVENING_REMINDER_HOUR && !remindedThisSit) { say(P_REMIND); remindedThisSit = true; }
    }
    wasSitting = sitting;

    // two missed days in a row -> tell the family once
    if (clockOk) {
      int day = currentDay(), n = historyCount();
      int lastMeasured = -1;
      for (int i = n - 1; i >= 0; i--) if (history()[i].measured) { lastMeasured = history()[i].day; break; }
      if (lastMeasured >= 0 && day - lastMeasured >= 3 && lastMissedSmsDay != day) {
        // record the missed days and let the detector decide: after a high score this is an ALERT, else a reminder
        DayRecord& missed = todayRecord(day - 1);
        Verdict v = evaluateToday(history(), historyCount());
        saveHistory();
        char msg[240];
        if (v.alert)
          snprintf(msg, sizeof(msg), "SAATHI ALERT %s: %s. Please visit or call them, and call the doctor today. Monitoring aid, not a diagnosis.",
                   settings().patientId, v.reason);
        else
          snprintf(msg, sizeof(msg), "SAATHI %s: no check for 2 days. Please remind them to sit and press green.", settings().patientId);
        (void)missed;
        smsSend(settings().caregiver, msg);
        lastMissedSmsDay = day;
      }
    }
    // low battery (no charger): tell the person once a day when they sit, and the family by SMS
    static uint32_t lastBattCheck = 0;
    if (millis() - lastBattCheck > 600000UL) {
      lastBattCheck = millis();
      int day = currentDay();
      if (!chargerPlugged() && batteryVolts() < BATTERY_LOW_V && lastBatteryWarnDay != day) {
        lastBatteryWarnDay = day;
        char msg[120];
        snprintf(msg, sizeof(msg), "SAATHI %s: the chair battery is low (%.2f V). Please plug in the charger.", settings().patientId, batteryVolts());
        smsSend(settings().caregiver, msg);
        say(P_BATTERY);
      }
    }
    if (smsPending()) ledSet((millis() / 1000) % 4 == 0);   // slow blink: a message is waiting for signal
  }

  if (greenPressed()) { lastActivity = millis(); demoAdvance(); runCheck(); lastActivity = millis(); }
  static uint32_t lastClock = 0;
  if (!clockOk && millis() - lastClock > 60000) { lastClock = millis(); syncClock(); }
  delay(5);
  idleSleep(sittingNow);
}
