// SAATHI chair firmware - voice prompts.
#include "voice.h"
#include "config.h"
#include "storage.h"
#include <LittleFS.h>

static const char* NAMES[P_COUNT] = {
  "start", "position", "hold", "q_ortho", "q_breath", "q_swell", "no_answer",
  "done_normal", "done_watch", "alert", "remind", "learning", "sos_sent", "finger",
  "other", "incomplete", "battery", "confirm"};

// English text; the same lines in Hindi and Tamil are in voice/prompts.csv (audio in voice/<lang>/)
static const char* EN[P_COUNT] = {
  "Start today's check? Press green.",
  "Unfold the footrest. Put your bare feet on the plates and one finger of your right hand in the groove.",
  "Please sit still for one and a half minutes.",
  "Last night, did you need extra pillows or sit up to breathe? Green for yes, red for no.",
  "Were you breathless today? Green for yes, red for no.",
  "Are your feet or ankles more swollen? Green for yes, red for no.",
  "Please press green for yes, or red for no.",
  "Today's check is done. Your readings are in your normal range.",
  "Today's check is done. Some readings are higher than usual. The chair will check again tomorrow.",
  "Your readings have been above your normal. Your family has been sent a message. Please call your doctor today.",
  "Today's check is not done. Press green to start.",
  "The chair is still learning your normal. Thank you for doing the check.",
  "Your family has been sent an SOS message.",
  "Please put one finger in the groove on the right armrest.",
  "Thank you. This check was not saved, because it was not the usual person.",
  "Some readings could not be taken. Please do the check again.",
  "The chair battery is low. Please plug in the charger.",
  "The weight is very different from usual. Is this the usual person doing the check? Green for yes, red for no."};

const char* promptName(Prompt p) { return NAMES[p]; }
const char* promptEnglish(Prompt p) { return EN[p]; }

void voiceBegin() { dacWrite(PIN_DAC_AUDIO, 128); }

// Non-blocking player: sayStart() opens the clip, sayService() must be called often (every < 125 us ideally);
// it writes every sample that is due and refills a small buffer from flash. This lets the chair speak the questions
// while the 90-second measurement loop keeps sampling. say() is the blocking version.
static File clip;
static uint8_t abuf[256];
static int alen = 0, apos = 0;
static uint32_t nextUs = 0;
static bool playing = false;

bool sayStart(Prompt p) {
  Serial.printf("[VOICE %s] %s\n", settings().lang, EN[p]);
#if SAATHI_DEMO
  playing = false;
  return false;
#else
  if (playing) { clip.close(); playing = false; }
  char path[40];
  snprintf(path, sizeof(path), "/voice/%s/%s.wav", settings().lang, NAMES[p]);
  clip = LittleFS.open(path, "r");
  if (!clip) { snprintf(path, sizeof(path), "/voice/en/%s.wav", NAMES[p]); clip = LittleFS.open(path, "r"); }
  if (!clip) return false;
  clip.seek(44);                                    // skip the WAV header (8-bit unsigned mono, 8000 Hz)
  alen = apos = 0;
  nextUs = micros();
  playing = true;
  return true;
#endif
}

bool sayService() {
  if (!playing) return false;
  int guard = 0;
  while ((int32_t)(micros() - nextUs) >= 0 && guard++ < 8) {   // catch up a few samples if the loop was slow
    if (apos >= alen) {
      alen = clip.read(abuf, sizeof(abuf)); apos = 0;
      if (alen <= 0) { clip.close(); playing = false; dacWrite(PIN_DAC_AUDIO, 128); return false; }
    }
    dacWrite(PIN_DAC_AUDIO, abuf[apos++]);
    nextUs += 125;                                  // 8 kHz
  }
  if ((int32_t)(micros() - nextUs) > 20000) nextUs = micros();   // a long stall: resync instead of racing
  return true;
}

bool sayBusy() { return playing; }

void sayStop() {
  if (playing) { clip.close(); playing = false; }
  dacWrite(PIN_DAC_AUDIO, 128);
}

void say(Prompt p) {
  if (!sayStart(p)) return;
  while (sayService()) {}
}
