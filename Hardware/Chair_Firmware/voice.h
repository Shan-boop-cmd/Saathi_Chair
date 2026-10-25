// SAATHI chair firmware - spoken prompts: 8 kHz 8-bit WAV clips in LittleFS, played on the ESP32 DAC -> PAM8403.
#pragma once
#include <Arduino.h>

enum Prompt {
  P_START, P_POSITION, P_HOLD, P_Q_ORTHO, P_Q_BREATH, P_Q_SWELL, P_NO_ANSWER,
  P_DONE_NORMAL, P_DONE_WATCH, P_ALERT, P_REMIND, P_LEARNING, P_SOS_SENT, P_FINGER,
  P_OTHER, P_INCOMPLETE, P_BATTERY, P_CONFIRM, P_COUNT
};

void voiceBegin();
void say(Prompt p);                 // blocks while speaking; prints the text when no clip is installed
bool sayStart(Prompt p);            // non-blocking: start a clip (false if none installed / demo build)
bool sayService();                  // call often while a clip plays; returns false when finished
bool sayBusy();
void sayStop();
const char* promptName(Prompt p);   // file name, e.g. "q_ortho"
const char* promptEnglish(Prompt p);
