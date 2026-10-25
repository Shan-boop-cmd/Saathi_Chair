// SAATHI chair firmware - settings and history storage.
#include "storage.h"
#include "config.h"
#include <Preferences.h>
#include <LittleFS.h>

static Settings S;
static Preferences prefs;
static DayRecord H[MAX_DAYS];
static int nH = 0;
static DemoState demo;

Settings& settings() { return S; }
DemoState& demoState() { return demo; }

void loadSettings() {
  prefs.begin("saathi", true);
  prefs.getString("care", S.caregiver, sizeof(S.caregiver));
  prefs.getString("lang", S.lang, sizeof(S.lang));
  prefs.getString("pid", S.patientId, sizeof(S.patientId));
  S.implant = prefs.getBool("implant", false);
  S.hxScale = prefs.getFloat("hxs", S.hxScale);
  S.hxOffset = prefs.getLong("hxo", 0);
  S.zCal = prefs.getFloat("zcal", 1.0f);
  S.setupEpoch = prefs.getULong("epoch", 0);
  prefs.getString("ssid", S.wifiSsid, sizeof(S.wifiSsid));
  prefs.getString("wpass", S.wifiPass, sizeof(S.wifiPass));
  prefs.getString("url", S.uploadUrl, sizeof(S.uploadUrl));
  prefs.end();
  if (!S.lang[0]) strcpy(S.lang, "en");
  if (!S.patientId[0]) strcpy(S.patientId, "SAATHI");
}

void saveSettings() {
  prefs.begin("saathi", false);
  prefs.putString("care", S.caregiver); prefs.putString("lang", S.lang); prefs.putString("pid", S.patientId);
  prefs.putBool("implant", S.implant); prefs.putFloat("hxs", S.hxScale); prefs.putLong("hxo", S.hxOffset);
  prefs.putFloat("zcal", S.zCal); prefs.putULong("epoch", S.setupEpoch);
  prefs.putString("ssid", S.wifiSsid); prefs.putString("wpass", S.wifiPass); prefs.putString("url", S.uploadUrl);
  prefs.end();
}

// Never format automatically: LittleFS.begin(true) would wipe the history and the SMS outbox after one bad mount.
// If mounting fails twice, the chair keeps working from RAM, says so, and only the console command
// "format-storage YES" erases the flash.
static bool fsOk = false;
bool storageOk() { return fsOk; }

bool formatStorage() {
  LittleFS.end();
  if (!LittleFS.format()) return false;
  fsOk = LittleFS.begin(false);
  return fsOk;
}

bool storageBegin() {
  fsOk = LittleFS.begin(false);
  if (!fsOk) { delay(200); fsOk = LittleFS.begin(false); }
  if (!fsOk) return false;
  File f = LittleFS.open("/history.bin", "r");
  if (f) {
    nH = f.read((uint8_t*)H, sizeof(H)) / sizeof(DayRecord);
    f.close();
  }
  return true;
}

DayRecord* history() { return H; }
int historyCount() { return nH; }

DayRecord& todayRecord(int dayIndex) {
  if (nH && H[nH - 1].day == dayIndex) return H[nH - 1];
  int from = nH ? H[nH - 1].day + 1 : 0;
  for (int d = from; d <= dayIndex; d++) {
    if (nH == MAX_DAYS) { memmove(H, H + 1, (MAX_DAYS - 1) * sizeof(DayRecord)); nH--; }   // keep the latest 120 days
    H[nH] = DayRecord(); H[nH].day = d; nH++;
  }
  return H[nH - 1];
}

void saveHistory() {
  if (!fsOk) return;                          // RAM-only mode: nothing written, nothing destroyed
  File f = LittleFS.open("/history.bin", "w");
  if (f) { f.write((const uint8_t*)H, nH * sizeof(DayRecord)); f.close(); }
}

void eraseHistory() { nH = 0; LittleFS.remove("/history.bin"); }

void printHistoryCsv(Stream& s) {
  s.println("day,measured,hour,weight_kg,foot_z_ohm,rest_hr_bpm,spo2_pct,resp_rate_bpm,ptt_ms,sit_count,q_orthopnea,q_breathless,q_swelling,score,alert");
  for (int i = 0; i < nH; i++) {
    const DayRecord& r = H[i];
    s.printf("%d,%d,%.2f,%.1f,%.0f,%.1f,%.1f,%.1f,%.0f,%d,%d,%d,%d,%.2f,%d\n", r.day, r.measured, r.hour, r.weight, r.footZ, r.hr,
             r.spo2, r.rr, r.ptt, r.sits, r.qOrtho, r.qBreath, r.qSwell, r.score, r.alert);
  }
}
