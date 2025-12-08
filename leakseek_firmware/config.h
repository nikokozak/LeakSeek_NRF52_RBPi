// LeakSeek Firmware Configuration
// ============================================

#ifndef CONFIG_H
#define CONFIG_H

// ============================================
// Manufacturer ID for BLE Advertising
// ============================================
// https://www.bluetooth.com/specifications/assigned-numbers/company-identifiers
// 0x018B is Konica Minolta, Inc. (placeholder - not officially assigned to us)
#define MANUFACTURER_ID 0x018B

// ============================================
// XIAO nRF52 Pin Assignments
// ============================================

// Water sensor: Analog voltage divider with ITO traces
// D5 drives 100k pull-up, A0 reads voltage divider
#define SENSOR_POWER_PIN 5     // GPIO output to drive pull-up resistor
#define SENSOR_READ_PIN A0     // Analog input for voltage divider

// Button for user interaction (INPUT_PULLUP, button to GND)
// Quick press: beep, 1s hold: stop alert, 2s hold: reboot
#define BUTTON_PIN 3

// Buzzer (3.3V piezo) - push-pull drive with square wave
#define BUZZER_PIN_POSITIVE 7
#define BUZZER_PIN_NEGATIVE 9

// ============================================
// BLE Configuration
// ============================================

// UUID16_SVC_ALERT_NOTIFICATION = 0x1811 (defined in BLEUuid.h)
#define SERVICE_UUID UUID16_SVC_ALERT_NOTIFICATION

// UUID16_CHR_ALERT_STATUS = 0x2A3F
#define CHARACTERISTIC_UUID UUID16_CHR_ALERT_STATUS

// Custom ACK characteristic UUID for clearing alerts
#define ACK_CHARACTERISTIC_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"

// Manufacturer data protocol version
#define PROTOCOL_VERSION 1

// Advertising modes
#define ADV_MODE_NORMAL 0
#define ADV_MODE_ALERT 1

// Advertising intervals (in units of 0.625ms)
#define ADV_INTERVAL_NORMAL 1600      // 1000ms between advertisements
#define ADV_INTERVAL_ALERT_FAST 32    // 20ms (fast burst for first 5 seconds)
#define ADV_INTERVAL_ALERT_SLOW 160   // 100ms (after initial burst)

// ============================================
// Sensor Timing
// ============================================

#define LOOP_DELAY_MS 100             // Check sensor every 100ms
#define SETTLING_TIME_MS 50           // Wait for RC filter to settle (5τ for 99%)

// ============================================
// Calibration Configuration
// ============================================
// On startup, the sensor takes multiple readings to establish a baseline.
// Thresholds are then calculated dynamically from this baseline.

#define CALIBRATION_SAMPLES 10        // Number of readings to average
#define CALIBRATION_DELAY_MS 100      // Delay between calibration readings

// Valid baseline range (10-bit ADC: 0-1023)
// Dry sensor should read high (~950-1020)
#define MIN_VALID_DRY_READING 800     // Below this = sensor shorted or wet at startup
#define MAX_VALID_DRY_READING 1020    // Above this = sensor disconnected

// Maximum allowed variance during calibration (std dev threshold)
#define MAX_CALIBRATION_VARIANCE 30   // If readings vary more than this, unstable

// ============================================
// Dynamic Threshold Offsets
// ============================================
// Thresholds are calculated from calibrated baseline:
//   threshold_wet = baseline - WET_OFFSET
//   threshold_dry = baseline - DRY_OFFSET
//
// The gap between them creates hysteresis to prevent oscillation.
// Example: baseline=1000 → wet<880, dry>950

#define WET_OFFSET 120                // How far below baseline triggers "wet"
#define DRY_OFFSET 50                 // How far below baseline clears "wet"

// Debounce: consecutive readings required to confirm state change
#define WATER_DETECTION_DEBOUNCE_COUNT 3

// ============================================
// Button Configuration
// ============================================

#define BUTTON_DEBOUNCE_MS 50         // Debounce time for button press
#define BUTTON_QUICK_PRESS_MAX_MS 500 // Max duration for "quick press"
#define BUTTON_STOP_HOLD_MS 1000      // 1 second hold to enter STOPPED state
#define BUTTON_REBOOT_HOLD_MS 2000    // 2 second hold to reboot

// ============================================
// Buzzer Configuration
// ============================================

// Alert beep pattern (leak detected)
#define BUZZER_BEEP_DURATION_MS 200   // Each beep lasts 200ms
#define BUZZER_BEEP_PAUSE_MS 1800     // Pause between beeps (2 second cycle)

// Square wave generation
#define BUZZER_FREQUENCY_HZ 4000      // 4kHz - typical piezo resonant frequency

// ============================================
// Error Beep Patterns
// ============================================
// Used during calibration to indicate sensor status

// Calibration OK: two short beeps
#define BEEP_OK_COUNT 2
#define BEEP_OK_DURATION_MS 100
#define BEEP_OK_PAUSE_MS 100

// Sensor error (shorted/wet at startup): rapid triple beep, repeated
#define BEEP_ERROR_COUNT 3
#define BEEP_ERROR_DURATION_MS 50
#define BEEP_ERROR_PAUSE_MS 50
#define BEEP_ERROR_REPEAT_INTERVAL_MS 1000

// Sensor disconnected: long beep, repeated
#define BEEP_DISCONNECTED_DURATION_MS 500
#define BEEP_DISCONNECTED_PAUSE_MS 500

// Unstable readings: alternating short-long pattern
#define BEEP_UNSTABLE_SHORT_MS 50
#define BEEP_UNSTABLE_LONG_MS 200
#define BEEP_UNSTABLE_PAUSE_MS 100

// ============================================
// System States
// ============================================

#define STATE_NORMAL 0                // Normal monitoring mode
#define STATE_ALERT 1                 // Alert mode (leak detected, buzzing)
#define STATE_STOPPED 2               // Stopped (acknowledged, waiting for reboot)
#define STATE_SENSOR_ERROR 3          // Sensor error detected during calibration

// ============================================
// Debug Configuration
// ============================================

// Debug modes:
// 0 = No debug output (production)
// 1 = Text debug (detailed messages)
// 2 = Graph debug (numeric values for Serial Plotter)
#define DEBUG_MODE 1

#define GRAPH_DEBUG_INTERVAL_MS 100   // Update graph every 100ms (mode 2 only)

// Debug Macros
#if DEBUG_MODE == 1
  #define DEBUG_PRINT(x) if (Serial) { Serial.println(x); }
  #define DEBUG_PRINTF(fmt, ...) if (Serial) { Serial.printf(fmt, ##__VA_ARGS__); }
#else
  #define DEBUG_PRINT(x) do {} while (0)
  #define DEBUG_PRINTF(fmt, ...) do {} while (0)
#endif

#endif // CONFIG_H
