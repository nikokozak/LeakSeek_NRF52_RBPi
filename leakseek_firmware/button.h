// LeakSeek Button Handling
// ============================================

#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>
#include "config.h"
#include "buzzer.h"

// ============================================
// Button State (exposed for debug)
// ============================================

extern bool button_pressed;
extern unsigned long button_press_start;

// ============================================
// Callback Type
// ============================================

// Callback for button actions
// action: 0 = quick press, 1 = stop hold (1s), 2 = reboot hold (2s)
typedef void (*ButtonCallback)(uint8_t action);

#define BUTTON_ACTION_QUICK 0
#define BUTTON_ACTION_STOP 1
#define BUTTON_ACTION_REBOOT 2

// ============================================
// Function Declarations
// ============================================

// Initialize button pin
void button_init();

// Check button state and handle actions
// system_state: current system state (affects which actions are available)
// callback: function to call when an action is triggered
void button_update(uint8_t system_state, ButtonCallback callback);

// ============================================
// Implementation
// ============================================

bool button_pressed = false;
unsigned long button_press_start = 0;

// Internal state
static bool _button_debounce_flag = false;
static unsigned long _button_debounce_time = 0;
static bool _button_action_taken = false;

void button_init() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void button_update(uint8_t system_state, ButtonCallback callback) {
  bool button_state = (digitalRead(BUTTON_PIN) == LOW);  // Active low

  // Debounce
  if (button_state != button_pressed) {
    if (!_button_debounce_flag) {
      _button_debounce_time = millis();
      _button_debounce_flag = true;
    } else if (millis() - _button_debounce_time > BUTTON_DEBOUNCE_MS) {
      button_pressed = button_state;
      _button_debounce_flag = false;

      if (button_pressed) {
        button_press_start = millis();
        _button_action_taken = false;
        DEBUG_PRINT("Button pressed");
      } else {
        // Button released - check for quick press
        unsigned long hold_time = millis() - button_press_start;
        if (!_button_action_taken && hold_time < BUTTON_QUICK_PRESS_MAX_MS) {
          DEBUG_PRINT("Quick press detected");
          beep_feedback();
          if (callback) callback(BUTTON_ACTION_QUICK);
        }
        DEBUG_PRINT("Button released");
      }
    }
  } else {
    _button_debounce_flag = false;
  }

  // Handle held button actions
  if (button_pressed && !_button_action_taken) {
    unsigned long hold_time = millis() - button_press_start;

    // 2-second hold: reboot (highest priority, works in any state)
    if (hold_time >= BUTTON_REBOOT_HOLD_MS) {
      DEBUG_PRINT("BUTTON HELD 2s - REBOOT");
      beep_confirm();
      _button_action_taken = true;
      if (callback) callback(BUTTON_ACTION_REBOOT);
      // Note: callback should handle the actual reboot
    }
    // 1-second hold in ALERT or SENSOR_ERROR: stop
    else if (hold_time >= BUTTON_STOP_HOLD_MS &&
             (system_state == STATE_ALERT || system_state == STATE_SENSOR_ERROR)) {
      DEBUG_PRINT("BUTTON HELD 1s - STOP");
      beep_feedback();
      _button_action_taken = true;
      if (callback) callback(BUTTON_ACTION_STOP);
    }
  }
}

#endif // BUTTON_H
