// LeakSeek Firmware v3.1 - Analog Voltage Divider with Auto-Calibration
// ============================================
//
// Features:
//   - Automatic sensor calibration on startup
//   - Dynamic thresholds based on calibrated baseline
//   - Error detection with distinct beep patterns
//   - Pulsed excitation prevents galvanic corrosion
//
// Hardware: XIAO nRF52840, 100k pull-up, ITO traces, button, piezo buzzer
//
// Wiring:
//   D5 ──[100kΩ]──┬── A0 (ADC)
//                 │
//              [ITO A]
//                 ↕
//              [ITO B]
//                 │
//                GND
//
//   D3 ── Button ── GND (uses INPUT_PULLUP)
//   D7/D9 ── Piezo buzzer (push-pull drive)

#define FIRMWARE_VERSION "3.1.0-analog"

#include <bluefruit.h>
#include "config.h"
#include "buzzer.h"
#include "sensor.h"
#include "button.h"
#include "ble.h"

// ============================================
// BLE Objects (required by ble.h)
// ============================================
BLEService leakseek_service = BLEService(SERVICE_UUID);
BLECharacteristic leakseek_characteristic = BLECharacteristic(CHARACTERISTIC_UUID);
BLECharacteristic ack_characteristic = BLECharacteristic(ACK_CHARACTERISTIC_UUID);
BLEDis bledis;

// ============================================
// Global State
// ============================================
uint8_t system_state = 255; // Initialize to invalid state to force first transition
uint8_t current_seq = 0;
uint8_t current_flags = 0;  // bit0=leak, bit1=needs_ack
uint8_t battery_percent = 100;

// Timing
unsigned long last_sensor_check = 0;
unsigned long last_error_beep = 0;

// Hardware watchdog (production only)
#if DEBUG_MODE == 0
  #define WDT_TIMEOUT_SECONDS 4
#endif

// ============================================
// Forward Declarations
// ============================================
void set_system_state(uint8_t new_state);
void handle_button_action(uint8_t action);
void ack_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len);

// ============================================
// System State Management
// ============================================
void set_system_state(uint8_t new_state) {
  DEBUG_PRINTF("set_system_state called: %d -> %d\n", system_state, new_state);
  if (new_state == system_state) return;

  DEBUG_PRINTF("State changing: %d -> %d\n", system_state, new_state);
  system_state = new_state;

  switch (new_state) {
    case STATE_NORMAL:
      current_flags = 0x00;
      ble_set_advertising(ADV_MODE_NORMAL, current_flags, current_seq, battery_percent);
      buzzer_off();
      DEBUG_PRINT("NORMAL mode - monitoring");
      break;

    case STATE_ALERT:
      current_seq++;
      current_flags = 0x03;  // leak=1, needs_ack=1
      ble_set_advertising(ADV_MODE_ALERT, current_flags, current_seq, battery_percent);
      buzzer_last_beep = 0;  // Beep immediately
      DEBUG_PRINT("!!! ALERT - WATER DETECTED !!!");
      break;

    case STATE_STOPPED:
      current_flags = 0x01;  // leak=1, needs_ack=0
      ble_set_advertising(ADV_MODE_NORMAL, current_flags, current_seq, battery_percent);
      buzzer_off();
      DEBUG_PRINT("STOPPED - hold 2s to reboot");
      break;

    case STATE_SENSOR_ERROR:
      current_flags = 0x00;
      ble_set_advertising(ADV_MODE_NORMAL, current_flags, current_seq, battery_percent);
      last_error_beep = 0;  // Beep immediately
      DEBUG_PRINT("SENSOR ERROR - check wiring");
      break;
  }
}

// ============================================
// Button Callback
// ============================================
void handle_button_action(uint8_t action) {
  switch (action) {
    case BUTTON_ACTION_QUICK:
      // Quick press - just feedback beep (already done in button.h)
      break;

    case BUTTON_ACTION_STOP:
      if (system_state == STATE_ALERT) {
        set_system_state(STATE_STOPPED);
      } else if (system_state == STATE_SENSOR_ERROR) {
        // Allow silencing error beeps, but stay in error state
        set_system_state(STATE_STOPPED);
      }
      break;

    case BUTTON_ACTION_REBOOT:
      delay(100);  // Let beep finish
      NVIC_SystemReset();
      break;
  }
}

// ============================================
// BLE ACK Callback
// ============================================
void ack_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
  if (len > 0 && data[0] == 0x01 && system_state == STATE_ALERT) {
    DEBUG_PRINT("BLE ACK received - buzzer continues until button press");
    current_flags = 0x01;  // leak=1, needs_ack=0
    ble_set_advertising(ADV_MODE_NORMAL, current_flags, current_seq, battery_percent);
  }
}

// ============================================
// LED Heartbeat
// ============================================
void update_led() {
  static unsigned long last_blink = 0;
  static bool led_state = false;
  unsigned long interval;

  switch (system_state) {
    case STATE_NORMAL:       interval = 2000; break;
    case STATE_ALERT:        interval = 100;  break;
    case STATE_SENSOR_ERROR: interval = 250;  break;
    default:                 interval = 500;  break;
  }

  if (millis() - last_blink > interval) {
    led_state = !led_state;
    digitalWrite(LED_BUILTIN, led_state);
    last_blink = millis();
  }
}

// ============================================
// Error Beep Handler (non-blocking)
// ============================================
void update_error_beep() {
  if (system_state != STATE_SENSOR_ERROR) return;

  unsigned long now = millis();
  if (now - last_error_beep >= BEEP_ERROR_REPEAT_INTERVAL_MS) {
    last_error_beep = now;

    // Play error pattern based on calibration status
    switch (sensor_calibration_status) {
      case CALIBRATION_ERROR_LOW:
        beep_error();  // Rapid triple beep
        break;
      case CALIBRATION_ERROR_HIGH:
        beep_disconnected();  // Long beep
        break;
      case CALIBRATION_ERROR_UNSTABLE:
        beep_unstable();  // Short-long pattern
        break;
    }
  }
}

// ============================================
// Debug Output
// ============================================
void print_debug() {
#if DEBUG_MODE == 1
  static unsigned long last_debug = 0;
  if (millis() - last_debug > 2000) {
    last_debug = millis();

    const char* status = sensor_water_detected ? "WET" : "DRY";
    const char* state_name;
    switch (system_state) {
      case STATE_NORMAL:       state_name = "NORMAL"; break;
      case STATE_ALERT:        state_name = "ALERT"; break;
      case STATE_STOPPED:      state_name = "STOPPED"; break;
      case STATE_SENSOR_ERROR: state_name = "ERROR"; break;
      default:                 state_name = "???"; break;
    }

    float voltage = (sensor_last_reading / 1023.0) * 3.3;

    DEBUG_PRINTF("ADC: %d (%.2fV) | Baseline: %d | Thresh: %d/%d | %s | State: %s\n",
                 sensor_last_reading, voltage, sensor_baseline,
                 sensor_threshold_wet, sensor_threshold_dry,
                 status, state_name);
  }
#endif

#if DEBUG_MODE == 2
  static unsigned long last_graph = 0;
  if (millis() - last_graph > GRAPH_DEBUG_INTERVAL_MS) {
    last_graph = millis();
    // Format for Arduino Serial Plotter
    Serial.print("Reading:");
    Serial.print(sensor_last_reading);
    Serial.print(",Baseline:");
    Serial.print(sensor_baseline);
    Serial.print(",WetThresh:");
    Serial.print(sensor_threshold_wet);
    Serial.print(",DryThresh:");
    Serial.println(sensor_threshold_dry);
  }
#endif
}

// ============================================
// Setup
// ============================================
void setup() {
  Serial.begin(115200);
  // Wait for serial (with timeout)
  unsigned long start = millis();
  while (!Serial && millis() - start < 2000) delay(10);

  DEBUG_PRINT("\n========================================");
  DEBUG_PRINT("LeakSeek v3.1 - Auto-Calibrating Sensor");
  DEBUG_PRINTF("Firmware: %s\n", FIRMWARE_VERSION);
  DEBUG_PRINT("========================================\n");

  // 1. Configure ADC for Calibration (BLE is OFF)
  // Use VDD (3.3V) reference for ratiometric readings.
  DEBUG_PRINT("Configuring ADC for calibration...");
  analogReference(AR_VDD4);
  analogReadResolution(10);
  DEBUG_PRINT("ADC configured (AR_VDD4, 10-bit)");

  // Initialize hardware
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  buzzer_init();
  DEBUG_PRINT("Buzzer: D7/D9");

  sensor_init();
  DEBUG_PRINT("Sensor: D5 (power), A0 (read)");

  button_init();
  DEBUG_PRINT("Button: D3 (INPUT_PULLUP)");

  // 2. Run Calibration (BLE is OFF, so no SoftDevice conflicts)
  DEBUG_PRINT("\n--- Starting Calibration ---");
  uint8_t cal_result = sensor_calibrate();

  if (cal_result == CALIBRATION_OK) {
    DEBUG_PRINT("Calibration successful!");
    beep_ok();  // Two short beeps

    // 3. Initialize BLE (SoftDevice init might reset ADC)
    DEBUG_PRINT("Initializing BLE stack...");
    ble_init();
    DEBUG_PRINT("Setting ACK callback...");
    ack_characteristic.setWriteCallback(ack_write_callback);
    
    // 4. RESTORE ADC Configuration (Critical!)
    // SoftDevice init often resets SAADC to internal 3.6V ref.
    DEBUG_PRINT("Restoring ADC config after BLE init...");
    analogReference(AR_VDD4);

    DEBUG_PRINT("Starting System (Advertising)...");
    set_system_state(STATE_NORMAL);
  } else {
    DEBUG_PRINT("CALIBRATION FAILED!");
    
    // Still init BLE for diagnostics
    ble_init();
    ack_characteristic.setWriteCallback(ack_write_callback);
    
    // Restore ADC here too just in case
    analogReference(AR_VDD4);

    // Enter error state
    set_system_state(STATE_SENSOR_ERROR);
  }

  // Hardware watchdog (production only)
#if DEBUG_MODE == 0
  NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos) |
                    (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos);
  NRF_WDT->CRV = 32768 * WDT_TIMEOUT_SECONDS;
  NRF_WDT->RREN |= WDT_RREN_RR0_Msk;
  NRF_WDT->TASKS_START = 1;
#endif

  DEBUG_PRINT("\n========================================");
  DEBUG_PRINT("Button: quick=beep, 1s=stop, 2s=reboot");
  DEBUG_PRINT("========================================\n");

  digitalWrite(LED_BUILTIN, LOW);
}

// ============================================
// Main Loop
// ============================================
void loop() {
  // Feed watchdog
#if DEBUG_MODE == 0
  NRF_WDT->RR[0] = WDT_RR_RR_Reload;
#endif

  // Sensor check (periodic, non-blocking)
  if (millis() - last_sensor_check >= LOOP_DELAY_MS) {
    last_sensor_check = millis();

    switch (system_state) {
      case STATE_NORMAL:
        if (sensor_check_water()) {
          set_system_state(STATE_ALERT);
        }
        break;

      case STATE_ALERT:
        sensor_check_water();  // Keep reading for state tracking
        ble_check_alert_timing();  // Handle fast->slow advertising transition
        break;

      case STATE_STOPPED:
        sensor_check_water();  // Keep reading for debug
        break;

      case STATE_SENSOR_ERROR:
        // Don't check sensor in error state
        break;
    }
  }

  // Update subsystems
  button_update(system_state, handle_button_action);
  buzzer_update_alert(system_state);
  update_error_beep();
  update_led();
  print_debug();
}
