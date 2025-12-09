// LeakSeek Buzzer Control
// ============================================

#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>
#include "config.h"

// ============================================
// Buzzer State
// ============================================

extern unsigned long buzzer_last_beep;
extern uint8_t error_beep_state;  // For continuous error patterns

// ============================================
// Core Buzzer Functions
// ============================================

// Initialize buzzer pins
void buzzer_init();

// Generate a blocking beep at configured frequency
void beep(int duration_ms);

// ============================================
// Feedback Patterns (blocking, one-shot)
// ============================================

// Calibration successful: two short beeps
void beep_ok();

// Quick button press feedback: single short beep
void beep_feedback();

// Stop/reboot confirmation: single medium beep
void beep_confirm();

// ============================================
// Error Patterns (blocking, one-shot)
// ============================================

// Sensor error pattern: rapid triple beep
void beep_error();

// Sensor disconnected pattern: long beep
void beep_disconnected();

// Unstable readings pattern: short-long alternating
void beep_unstable();

// ============================================
// Alert Pattern (non-blocking, called from loop)
// ============================================

// Update alert buzzer - call this every loop iteration
// Only beeps when system is in STATE_ALERT
void buzzer_update_alert(uint8_t system_state);

// Turn off buzzer immediately
void buzzer_off();

// ============================================
// Implementation
// ============================================

unsigned long buzzer_last_beep = 0;
uint8_t error_beep_state = 0;

void buzzer_init() {
  pinMode(BUZZER_PIN_POSITIVE, OUTPUT);
  pinMode(BUZZER_PIN_NEGATIVE, OUTPUT);
  digitalWrite(BUZZER_PIN_POSITIVE, LOW);
  digitalWrite(BUZZER_PIN_NEGATIVE, LOW);
}

void beep(int duration_ms) {
#if BUZZER_MUTED
  // Muted - just delay for the same duration
  delay(duration_ms);
  return;
#endif

  // Generate blocking square wave at configured frequency
  int half_period_us = 1000000 / (BUZZER_FREQUENCY_HZ * 2);
  int cycles = (duration_ms * 1000) / (half_period_us * 2);

  for (int i = 0; i < cycles; i++) {
    digitalWrite(BUZZER_PIN_POSITIVE, HIGH);
    digitalWrite(BUZZER_PIN_NEGATIVE, LOW);
    delayMicroseconds(half_period_us);
    digitalWrite(BUZZER_PIN_POSITIVE, LOW);
    digitalWrite(BUZZER_PIN_NEGATIVE, HIGH);
    delayMicroseconds(half_period_us);
  }

  // Turn off
  digitalWrite(BUZZER_PIN_POSITIVE, LOW);
  digitalWrite(BUZZER_PIN_NEGATIVE, LOW);
}

void buzzer_off() {
  digitalWrite(BUZZER_PIN_POSITIVE, LOW);
  digitalWrite(BUZZER_PIN_NEGATIVE, LOW);
}

// ============================================
// Feedback Patterns
// ============================================

void beep_ok() {
  // Two short beeps for calibration success
  for (int i = 0; i < BEEP_OK_COUNT; i++) {
    beep(BEEP_OK_DURATION_MS);
    if (i < BEEP_OK_COUNT - 1) {
      delay(BEEP_OK_PAUSE_MS);
    }
  }
}

void beep_feedback() {
  // Single short beep for button feedback
  beep(50);
}

void beep_confirm() {
  // Single medium beep for confirmations
  beep(100);
}

// ============================================
// Error Patterns
// ============================================

void beep_error() {
  // Rapid triple beep for sensor error
  for (int i = 0; i < BEEP_ERROR_COUNT; i++) {
    beep(BEEP_ERROR_DURATION_MS);
    if (i < BEEP_ERROR_COUNT - 1) {
      delay(BEEP_ERROR_PAUSE_MS);
    }
  }
}

void beep_disconnected() {
  // Long beep for disconnected sensor
  beep(BEEP_DISCONNECTED_DURATION_MS);
}

void beep_unstable() {
  // Short-long alternating pattern
  beep(BEEP_UNSTABLE_SHORT_MS);
  delay(BEEP_UNSTABLE_PAUSE_MS);
  beep(BEEP_UNSTABLE_LONG_MS);
}

// ============================================
// Alert Pattern (non-blocking)
// ============================================

void buzzer_update_alert(uint8_t system_state) {
  if (system_state != STATE_ALERT) {
    buzzer_off();
    return;
  }

  // Alert mode - periodic beep pattern
  unsigned long now = millis();
  if (now - buzzer_last_beep >= (BUZZER_BEEP_DURATION_MS + BUZZER_BEEP_PAUSE_MS)) {
    buzzer_last_beep = now;
    beep(BUZZER_BEEP_DURATION_MS);
  }
}

#endif // BUZZER_H
