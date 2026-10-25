// SAATHI chair firmware - SIM800L SMS over AT commands.
#include "sms.h"
#include "config.h"
#include <time.h>
#include <LittleFS.h>
#include <vector>
#include "outbox.h"

// Outbox (outbox.h): kept in flash (/outbox.txt) so a power cut or no signal never loses a message; retried every
// 5 minutes; alerts are never evicted; long messages resume at the next unsent part (no duplicate parts).
static Outbox box;
static uint32_t lastTry = 0;
extern bool storageOk();
static void saveOutbox() {
  if (!storageOk()) return;
  File f = LittleFS.open("/outbox.txt", "w");
  if (!f) return;
  std::string s = box.serialize();
  f.write((const uint8_t*)s.data(), s.size());
  f.close();
}
static void loadOutbox() {
  if (!storageOk()) return;
  File f = LittleFS.open("/outbox.txt", "r");
  if (!f) return;
  std::string s;
  while (f.available()) s += (char)f.read();
  f.close();
  box.deserialize(s);
}

static HardwareSerial& gsm = Serial2;

static bool waitFor(const char* token, uint32_t ms, String* got = nullptr) {
  String buf; uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    while (gsm.available()) { buf += (char)gsm.read(); if (buf.indexOf(token) >= 0) { if (got) *got = buf; return true; } }
    delay(5);
  }
  if (got) *got = buf;
  return false;
}

static bool at(const char* cmd, const char* expect = "OK", uint32_t ms = 2000) {
  gsm.print(cmd); gsm.print("\r");
  return waitFor(expect, ms);
}

void smsBegin() {
#if !SAATHI_DEMO
  pinMode(PIN_SIM_RST, OUTPUT);
  digitalWrite(PIN_SIM_RST, LOW); delay(200); digitalWrite(PIN_SIM_RST, HIGH);
  gsm.begin(9600, SERIAL_8N1, PIN_SIM_RX, PIN_SIM_TX);
  delay(3000);
  for (int i = 0; i < 5 && !at("AT"); i++) delay(500);
  at("ATE0");
  at("AT+CMGF=1");          // text mode
  at("AT+CLTS=1");          // take local time from the network
  at("AT+CSCLK=2");         // auto-sleep when the UART is idle (~1 mA); wakes on the next command
#endif
  loadOutbox();
  if (box.q.size()) Serial.printf("[SMS] %u message(s) waiting from before the restart\n", (unsigned)box.q.size());
}

static void wake() {
#if !SAATHI_DEMO
  for (int i = 0; i < 3; i++) { if (at("AT", "OK", 300)) return; }   // first character after sleep may be lost
#endif
}

static bool signalOk() {
#if SAATHI_DEMO
  return true;
#else
  String r;
  gsm.print("AT+CSQ\r");
  if (!waitFor("OK", 2000, &r)) return false;
  int q = r.indexOf("+CSQ:");
  if (q < 0) return false;
  int rssi = r.substring(q + 5).toInt();
  return rssi > 0 && rssi != 99;
#endif
}

static bool sendOne(const char* number, const char* text) {
#if SAATHI_DEMO
  Serial.printf("[SMS to %s] %s\n", number[0] ? number : "(no number set)", text);
  return true;
#else
  if (!number[0]) { Serial.println("[SMS] no caregiver number set"); return false; }
  char cmd[40]; snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);
  gsm.print(cmd); gsm.print("\r");
  if (!waitFor(">", 5000)) return false;
  gsm.print(text);
  gsm.write(26);            // Ctrl+Z sends
  bool ok = waitFor("+CMGS", 20000);
  Serial.printf("[SMS %s] %s\n", ok ? "sent" : "FAILED", text);
  return ok;
#endif
}

static void flush() {
  lastTry = millis();
  if (box.q.empty()) return;
  wake();
  if (!signalOk()) { Serial.printf("[SMS] no network signal; %u message(s) kept for retry\n", (unsigned)box.q.size()); return; }
  box.flush([](const std::string& num, const std::string& part) { return sendOne(num.c_str(), part.c_str()); }, saveOutbox);
}

bool smsSend(const char* number, const char* text) {
  if (!number[0]) {
#if SAATHI_DEMO
    return sendOne(number, text);             // demo: just print it
#else
    Serial.println("[SMS] no caregiver number set"); return false;
#endif
  }
  if (!box.enqueue(number, text)) Serial.printf("[SMS] %s\n", box.log.c_str());
  else if (!box.log.empty()) { Serial.printf("[SMS] %s\n", box.log.c_str()); box.log.clear(); }
  saveOutbox();
  flush();
  return box.q.empty();
}

void smsService() {
  if (!box.q.empty() && millis() - lastTry > 5UL * 60UL * 1000UL) flush();
}

int smsPending() { return (int)box.q.size(); }

bool networkTime(uint32_t* epochOut) {
#if SAATHI_DEMO
  return false;
#else
  String r;
  wake();
  gsm.print("AT+CCLK?\r");
  if (!waitFor("OK", 2000, &r)) return false;
  int q = r.indexOf('"');                       // +CCLK: "yy/MM/dd,hh:mm:ss+zz"
  if (q < 0) return false;
  struct tm t = {};
  if (sscanf(r.c_str() + q + 1, "%2d/%2d/%2d,%2d:%2d:%2d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) != 6) return false;
  if (t.tm_year < 24) return false;             // network time not received yet
  t.tm_year += 100; t.tm_mon -= 1;
  *epochOut = (uint32_t)mktime(&t);
  return true;
#endif
}
