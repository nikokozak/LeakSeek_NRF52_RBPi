// LeakSeek Water Sensor with Calibration
// ============================================

#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include "config.h"
#include "buzzer.h"

// ============================================
// Calibration Result Codes
// ============================================

#define CALIBRATION_OK 0
#define CALIBRATION_ERROR_LOW 1       // Reading too low (shorted or wet)
#define CALIBRATION_ERROR_HIGH 2      // Reading too high (disconnected)
#define CALIBRATION_ERROR_UNSTABLE 3  // Readings too variable

// ============================================
// Sensor State (exposed for debug output)
// ============================================

extern int sensor_baseline;           // Calibrated dry baseline
extern int sensor_threshold_wet;      // Dynamic wet threshold
extern int sensor_threshold_dry;      // Dynamic dry threshold
extern int sensor_last_reading;       // Most recent ADC reading
extern bool sensor_water_detected;    // Current water state
extern uint8_t sensor_calibration_status;  // Result of calibration

// ============================================
// Function Declarations
// ============================================

// Initialize sensor pins
void sensor_init();

// Run calibration routine (call once during setup)
// Returns: CALIBRATION_OK or error code
// Side effects: sets sensor_baseline, thresholds, and calibration_status
uint8_t sensor_calibrate();

// Take a single sensor reading (handles pulsed excitation)
int sensor_read();

// Check for water with hysteresis and debounce
// Returns: true if water currently detected
bool sensor_check_water();

// ============================================
// Implementation
// ============================================

int sensor_baseline = 0;
int sensor_threshold_wet = 0;
int sensor_threshold_dry = 0;
int sensor_last_reading = 0;
bool sensor_water_detected = false;
uint8_t sensor_calibration_status = CALIBRATION_OK;

// Internal state
static int _debounce_counter = 0;

void sensor_init() {
  pinMode(SENSOR_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, LOW);
  analogReadResolution(10);  // 10-bit ADC (0-1023)
}

int sensor_read() {
  // Power on sensor (drive pull-up high)
  digitalWrite(SENSOR_POWER_PIN, HIGH);

  // Wait for RC filter to settle
  delay(SETTLING_TIME_MS);

  // Read ADC
  int reading = analogRead(SENSOR_READ_PIN);

  // Power off sensor (prevent galvanic corrosion)
  digitalWrite(SENSOR_POWER_PIN, LOW);

  sensor_last_reading = reading;
  return reading;
}

uint8_t sensor_calibrate() {
  DEBUG_PRINT("Starting sensor calibration...");

  int readings[CALIBRATION_SAMPLES];
  long sum = 0;

  // Collect calibration samples
  for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
    readings[i] = sensor_read();
    sum += readings[i];
    DEBUG_PRINTF("  Sample %d: %d\n", i + 1, readings[i]);
    delay(CALIBRATION_DELAY_MS);
  }

  // Calculate average (baseline)
  sensor_baseline = sum / CALIBRATION_SAMPLES;
  DEBUG_PRINTF("Baseline average: %d\n", sensor_baseline);

  // Calculate variance (simple: max - min)
  int min_reading = readings[0];
  int max_reading = readings[0];
  for (int i = 1; i < CALIBRATION_SAMPLES; i++) {
    if (readings[i] < min_reading) min_reading = readings[i];
    if (readings[i] > max_reading) max_reading = readings[i];
  }
  int variance = max_reading - min_reading;
  DEBUG_PRINTF("Variance (max-min): %d\n", variance);

  // Validate baseline
  if (sensor_baseline < MIN_VALID_DRY_READING) {
    DEBUG_PRINT("ERROR: Baseline too low - sensor shorted or wet at startup");
    sensor_calibration_status = CALIBRATION_ERROR_LOW;
    return CALIBRATION_ERROR_LOW;
  }

  if (sensor_baseline > MAX_VALID_DRY_READING) {
    DEBUG_PRINT("ERROR: Baseline too high - sensor may be disconnected");
    sensor_calibration_status = CALIBRATION_ERROR_HIGH;
    return CALIBRATION_ERROR_HIGH;
  }

  if (variance > MAX_CALIBRATION_VARIANCE) {
    DEBUG_PRINT("ERROR: Readings too unstable");
    sensor_calibration_status = CALIBRATION_ERROR_UNSTABLE;
    return CALIBRATION_ERROR_UNSTABLE;
  }

  // Calculate dynamic thresholds
  sensor_threshold_wet = sensor_baseline - WET_OFFSET;
  sensor_threshold_dry = sensor_baseline - DRY_OFFSET;

  // Clamp thresholds to valid range
  if (sensor_threshold_wet < 0) sensor_threshold_wet = 0;
  if (sensor_threshold_dry < 0) sensor_threshold_dry = 0;

  DEBUG_PRINTF("Calibration OK!\n");
  DEBUG_PRINTF("  Baseline: %d\n", sensor_baseline);
  DEBUG_PRINTF("  Wet threshold: <%d\n", sensor_threshold_wet);
  DEBUG_PRINTF("  Dry threshold: >%d\n", sensor_threshold_dry);
  DEBUG_PRINTF("  Hysteresis gap: %d\n", sensor_threshold_dry - sensor_threshold_wet);

  sensor_calibration_status = CALIBRATION_OK;
  return CALIBRATION_OK;
}

bool sensor_check_water() {
  int reading = sensor_read();

  // Hysteresis state machine with debounce
  if (!sensor_water_detected) {
    // Currently DRY - check for wet
    if (reading < sensor_threshold_wet) {
      _debounce_counter++;
      if (_debounce_counter >= WATER_DETECTION_DEBOUNCE_COUNT) {
        sensor_water_detected = true;
        _debounce_counter = 0;
        DEBUG_PRINTF("WATER DETECTED! ADC: %d (threshold: %d)\n", reading, sensor_threshold_wet);
        return true;
      }
    } else {
      _debounce_counter = 0;  // Reset if reading goes back up
    }
  } else {
    // Currently WET - check for dry
    if (reading > sensor_threshold_dry) {
      _debounce_counter++;
      if (_debounce_counter >= WATER_DETECTION_DEBOUNCE_COUNT) {
        sensor_water_detected = false;
        _debounce_counter = 0;
        DEBUG_PRINTF("Water cleared. ADC: %d (threshold: %d)\n", reading, sensor_threshold_dry);
      }
    } else {
      _debounce_counter = 0;  // Reset if reading drops back down
    }
  }

  return sensor_water_detected;
}

#endif // SENSOR_H
