// SAATHI chair firmware - configuration. Pins match hardware/schematic_system.png.
#pragma once
#include <Arduino.h>

// Demo build for Wokwi or a bare ESP32 (sensors simulated, SMS and voice printed to the serial monitor):
// put a file named demo_mode.h in this folder (the Wokwi copy in firmware/wokwi has one). Real chair: no such file.
#if __has_include("demo_mode.h")
#include "demo_mode.h"
#endif
#ifndef SAATHI_DEMO
#define SAATHI_DEMO 0
#endif

// ---------------- pins ----------------
constexpr int PIN_BCG_ADC      = 34;   // seat PVDF front-end output (ADC1_CH6)
constexpr int PIN_BATT_ADC     = 35;   // battery / 2
constexpr int PIN_Z_ADC        = 36;   // foot-impedance rectifier DC level (ADC1_CH0)
constexpr int PIN_CHARGER      = 39;   // HIGH = charger plugged in (impedance test blocked)
constexpr int PIN_I2C_SDA      = 21;   // MAX30102
constexpr int PIN_I2C_SCL      = 22;
constexpr int PIN_PPG_INT      = 19;
constexpr int PIN_HX_DOUT      = 4;    // HX711 (4 leg load cells)
constexpr int PIN_HX_SCK       = 5;
constexpr int PIN_Z_MODE_BODY  = 13;   // CD4066: heel plates -> amplifier
constexpr int PIN_Z_MODE_CURR  = 14;   // CD4066: 470 ohm current sense -> amplifier
constexpr int PIN_Z_DRIVE      = 2;    // 50 kHz LEDC drive
constexpr int PIN_DAC_AUDIO    = 25;   // voice -> PAM8403
constexpr int PIN_LED          = 18;   // LED ring in the green button
constexpr int PIN_SIM_RX       = 16;   // ESP32 RX2 <- SIM800L TXD
constexpr int PIN_SIM_TX       = 17;   // ESP32 TX2 -> SIM800L RXD (via 1k/2k divider)
constexpr int PIN_SIM_RST      = 23;
constexpr int PIN_BTN_GREEN    = 32;   // yes / start
constexpr int PIN_BTN_RED      = 33;   // no
constexpr int PIN_BTN_SOS      = 27;   // SOS

// ---------------- check timing ----------------
constexpr uint32_t CHECK_SECONDS   = 90;    // measurement length
constexpr uint32_t SETTLE_SECONDS  = 6;     // ignored at the start (person settling)
constexpr uint32_t BCG_HZ          = 250;   // stored BCG rate (ADC sampled at 500 Hz, pairs averaged)
constexpr uint32_t PPG_HZ          = 100;   // MAX30102 sample rate
constexpr uint32_t ANSWER_TIMEOUT_MS = 20000;
constexpr float    SIT_KG          = 20.0f; // weight above this = someone is sitting

// ---------------- quality gates (same values as pipeline.py; see reports/raw_fault_injection.md) ----------------
constexpr float FINGER_IR_MIN       = 5000.0f; // MAX30102 IR counts with a finger present (ambient light ~1,000)
constexpr float FINGER_PRESENT_FRAC = 0.9f;    // finger present for 90% of the measurement
constexpr float PPG_Q_MIN           = 0.8f;    // beat consistency needed before SpO2 / breathing are trusted
constexpr float SPO2_RATIO_SPREAD   = 0.15f;   // per-beat red/IR ratios must agree (movement makes them scatter)
constexpr float RR_AGREE_BPM        = 2.5f;    // the breathing estimates must agree within this
constexpr float WEIGHT_MIN_KG = 25.0f, WEIGHT_MAX_KG = 200.0f;   // outside: broken bridge wire or not a person
constexpr float FULL_LOAD_FRAC      = 0.5f;    // must sit at full weight for half the measurement

// ---------------- foot impedance ----------------
constexpr uint32_t Z_DRIVE_HZ      = 50000;
constexpr float    Z_RCS_OHM       = 470.0f;
constexpr int      Z_ADC_SAMPLES   = 512;

// ---------------- detector (same values as detector.py / data/detector_params.json) ----------------
constexpr int   BASELINE_DAYS   = 14;
constexpr int   BASE_LAG        = 11;     // days a reading must age before it can update the baseline
constexpr float BASE_EXCLUDE    = 2.0f;   // days scoring at least this never update the baseline
constexpr int   BASE_WINDOW     = 28;     // most recent pool values used
constexpr float ALERT_THRESHOLD = 1.25f;  // chosen on training patients only
constexpr float WEIGHT_KG_PER_H = 0.071f; // time-of-day correction, learned on training patients
constexpr float Q_WEIGHT        = 0.70f;  // points per 'yes'
constexpr float DANGER_KG_3D    = 2.0f;   // weight +2 kg within 3 days
constexpr float SPO2_DROP       = 4.0f;   // SpO2 this many points below the person's own normal, 2 scored days running
constexpr float OTHER_PERSON_KG = 8.0f;   // weight this far from the patient's normal: probably someone else
constexpr int   MAX_DAYS        = 120;    // days of history kept in RAM

// ---------------- messages ----------------
constexpr int   EVENING_REMINDER_HOUR = 18;  // remind on sitting if no check by this hour
