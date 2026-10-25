// SAATHI chair firmware - settings (Preferences) and daily history (LittleFS), works fully offline.
#pragma once
#include <Arduino.h>
#include "detector.h"

struct Settings {
  char caregiver[20] = "";       // phone number for SMS, e.g. +9198xxxxxxxx
  char lang[3] = "en";           // voice language: en, hi, ta
  char patientId[12] = "SAATHI";
  bool implant = false;          // pacemaker / ICD -> foot impedance never runs
  float hxScale = 21000.0f;      // load-cell counts per kg (set with 'cal <kg>')
  long hxOffset = 0;             // empty-chair reading (set with 'tare')
  float zCal = 1.0f;             // one-time impedance factor (set with 'zcal <ohm>' on a known resistor)
  uint32_t setupEpoch = 0;       // unix time of first use (0 = unknown -> one day per check)
  char wifiSsid[33] = "", wifiPass[65] = "", uploadUrl[96] = "";
};

Settings& settings();
void loadSettings();
void saveSettings();

// History: one DayRecord per day since first use (oldest first), kept in RAM and in /history.bin
bool storageBegin();                    // false = flash could not be mounted: RAM-only mode, nothing formatted
bool storageOk();
bool formatStorage();                   // only from the console: "format-storage YES"
DayRecord* history();
int historyCount();
DayRecord& todayRecord(int dayIndex);   // creates the record (and any missed days before it) if needed
void saveHistory();
void eraseHistory();
void printHistoryCsv(Stream& s);

// Demo build: the simulated patient
struct DemoState {
  bool sitting = false;
  float trueWeight = 64.0f, trueHr = 72.0f, trueSpo2 = 96.0f, trueRr = 15.0f, trueZ = 520.0f;
};
DemoState& demoState();
