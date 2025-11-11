// LeakSeek Firmware v2.3 - RC Timing Water Sensor for XIAO nRF52 Custom PCB
// Uses RC time constant measurement with digital pins 7 & 8 (no ADC required!)
// Hardware: CR2032 powered, ITO trace water sensor (pins 7/8), button, buzzer
// Pin 8 has 100nF capacitor to GND for RC timing measurement
// See: https://github.com/adafruit/Adafruit_nRF52_Arduino/tree/master/libraries/Bluefruit52Lib/src

#define DEBUG true
#define FIRMWARE_VERSION "2.3.0-rc-timing"

#include <bluefruit.h>
#include <string.h>
#include "config.h"

// ============================================
// Global State Variables
// ============================================
uint8_t current_seq = 0;
uint8_t current_flags = 0;  // bit0=leak, bit1=needs_ack
uint8_t battery_percent = 100;
uint8_t adv_mode = ADV_MODE_NORMAL;
unsigned long alert_start_time = 0;
const unsigned long FAST_ADV_DURATION = 5000; // 5 seconds of fast advertising

// RC timing water detection state
unsigned long rc_time_us = 0;  // Last measured RC time in microseconds
unsigned long rc_time_history[RC_SAMPLE_COUNT];  // Initialized in setup()
int rc_history_index = 0;
bool water_detected = false;
unsigned long water_detect_time = 0;

// Adaptive baseline tracking
unsigned long rc_baseline_us = 50000;  // Learned baseline (default 50ms)
int baseline_sample_count = 0;         // How many samples collected for baseline
bool baseline_established = false;     // Has baseline been learned?
unsigned long baseline_update_time = 0; // Last time we updated baseline

// Button state
bool button_pressed = false;
unsigned long button_press_start = 0;
bool button_debounce_flag = false;
unsigned long button_debounce_time = 0;

// Buzzer state (simplified for blocking beep pattern)
unsigned long buzzer_last_beep = 0;

// System state machine
uint8_t system_state = STATE_NORMAL;

// Watchdog and reliability
unsigned long last_loop_time = 0;
unsigned long loop_iterations = 0;

// BLE service and characteristics
BLEService leakseek_service = BLEService(SERVICE_UUID);
BLECharacteristic leakseek_characteristic = BLECharacteristic(CHARACTERISTIC_UUID);
BLECharacteristic ack_characteristic = BLECharacteristic(ACK_CHARACTERISTIC_UUID);
BLEDis bledis;

// Hardware watchdog (4 second timeout)
#if DEBUG
  // Disabled in debug mode to allow Serial debugging
#else
  #include <nrf_wdt.h>
  #define WDT_TIMEOUT_SECONDS 4
#endif

// ============================================
// Forward Declarations
// ============================================
void set_advertising_mode(uint8_t mode);
void ack_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len);
unsigned long measure_rc_time();
unsigned long read_water_sensor();
bool check_water_detected();
void check_button();
void update_buzzer();
void set_system_state(uint8_t new_state);
void print_water_debug();
void print_graph_debug();

// ============================================
// RC Timing Measurement Function
// ============================================
unsigned long measure_rc_time() {
  // Configure pins for discharge
  pinMode(WATER_SENSE_PIN_A, OUTPUT);
  pinMode(WATER_SENSE_PIN_B, INPUT);  // NO pulldown - would prevent charging!
  digitalWrite(WATER_SENSE_PIN_A, LOW);

  // Use delayMicroseconds instead of delay for faster, more predictable timing
  // 10ms = 10000 microseconds - enough for full discharge
  delayMicroseconds(10000);

  // Check if pin is stuck HIGH after discharge (disconnected/floating)
  if (digitalRead(WATER_SENSE_PIN_B) == HIGH) {
    // Sensor disconnected or floating - return error value immediately
    static unsigned long last_warning = 0;
    if (millis() - last_warning > 1000) {  // Limit debug spam
      DEBUG_PRINT("WARNING: Sensor disconnected");
      last_warning = millis();
    }
    return 150000;  // Return high value but not timeout
  }

  // Start charging
  digitalWrite(WATER_SENSE_PIN_A, HIGH);
  unsigned long start = micros();

  // Timeout loop - allow up to 200ms for high-resistance dry traces
  // With 330k + 1M traces and 100nF cap, RC time can be ~133ms
  while (digitalRead(WATER_SENSE_PIN_B) == LOW) {
    unsigned long elapsed = micros() - start;

    // Timeout after 200ms - allows for high-resistance dry traces
    if (elapsed > 200000) {
      digitalWrite(WATER_SENSE_PIN_A, LOW);
      static unsigned long last_timeout = 0;
      if (millis() - last_timeout > 1000) {  // Limit debug spam
        DEBUG_PRINT("WARNING: RC timeout (>200ms)");
        last_timeout = millis();
      }
      return 200000;  // Return timeout value
    }
  }

  unsigned long charge_time = micros() - start;

  // Leave pin in known state
  digitalWrite(WATER_SENSE_PIN_A, LOW);

  return charge_time;
}

// ============================================
// Water Sensor Reading with Rolling Average
// ============================================
unsigned long read_water_sensor() {
  // Take new RC timing measurement
  unsigned long current_time = measure_rc_time();

  // Reject obviously bad readings (disconnected sensor or timeout)
  // Values >150ms indicate disconnected or timeout
  // With 100nF cap and high-resistance traces, dry can be 45-150ms
  if (current_time > RC_ABSOLUTE_MAX_DRY_US) {
    // Don't update history buffer with bad reading
    // Return last good average or a high value if no good history
    unsigned long sum = 0;
    int valid_count = 0;

    for (int i = 0; i < RC_SAMPLE_COUNT; i++) {
      if (rc_time_history[i] < RC_ABSOLUTE_MAX_DRY_US) {
        sum += rc_time_history[i];
        valid_count++;
      }
    }

    if (valid_count > 0) {
      return sum / valid_count;  // Average of valid samples
    } else {
      return RC_ABSOLUTE_MAX_DRY_US;  // All samples bad, return max dry value
    }
  }

  // Store good reading in history buffer
  rc_time_history[rc_history_index] = current_time;
  rc_history_index = (rc_history_index + 1) % RC_SAMPLE_COUNT;

  // Calculate average of samples
  unsigned long sum = 0;
  for (int i = 0; i < RC_SAMPLE_COUNT; i++) {
    sum += rc_time_history[i];
  }

  unsigned long average_time = sum / RC_SAMPLE_COUNT;
  return average_time;
}

// ============================================
// Water Detection Logic - ADAPTIVE BASELINE
// ============================================
bool check_water_detected() {
  unsigned long avg_time_us = read_water_sensor();
  rc_time_us = avg_time_us;  // Store for debug output

  // Step 1: Establish baseline during first RC_BASELINE_SAMPLES readings
  if (!baseline_established && baseline_sample_count < RC_BASELINE_SAMPLES) {
    // Reject obviously bad readings during baseline learning
    // Accept readings up to RC_ABSOLUTE_MAX_DRY_US (150ms with 100nF cap)
    if (avg_time_us > RC_ABSOLUTE_MAX_DRY_US || avg_time_us < 1000) {
      DEBUG_PRINT("Skipping bad reading during baseline: " + String(avg_time_us) + " us");
      return false;  // Don't count this sample
    }

    // Accumulate baseline average
    if (baseline_sample_count == 0) {
      rc_baseline_us = avg_time_us;  // First sample
    } else {
      // Running average: new_avg = old_avg + (new_value - old_avg) / count
      rc_baseline_us = rc_baseline_us + (avg_time_us - rc_baseline_us) / (baseline_sample_count + 1);
    }
    baseline_sample_count++;

    DEBUG_PRINT("Baseline learning: sample " + String(baseline_sample_count) +
                ", current avg: " + String(rc_baseline_us) + " us");

    if (baseline_sample_count >= RC_BASELINE_SAMPLES) {
      baseline_established = true;
      baseline_update_time = millis();
      DEBUG_PRINT("*** BASELINE ESTABLISHED: " + String(rc_baseline_us) + " us ***");
    }

    return false;  // No alerts during baseline learning
  }

  // Step 2: Adaptive threshold based on percentage drop from baseline
  // Calculate dynamic threshold: baseline * (100 - DROP_PERCENTAGE) / 100
  // Example: If baseline=50ms and drop=80%, threshold = 50 * 20/100 = 10ms
  unsigned long dynamic_threshold = (rc_baseline_us * (100 - RC_WATER_DROP_PERCENTAGE)) / 100;

  // Clamp to absolute minimum (safety: anything <10ms is definitely wet)
  if (dynamic_threshold < RC_ABSOLUTE_MIN_THRESHOLD_US) {
    dynamic_threshold = RC_ABSOLUTE_MIN_THRESHOLD_US;
  }

  // Step 3: Determine if water detected using dynamic OR absolute threshold
  bool water_now;

  #if RC_DETECTION_INVERTED
    // Inverted mode (unusual): water INCREASES charge time
    water_now = (avg_time_us > dynamic_threshold) || (avg_time_us > RC_ABSOLUTE_MIN_THRESHOLD_US);
  #else
    // Normal mode: water DECREASES charge time
    // Water detected if EITHER:
    //   1. Reading drops below dynamic threshold (percentage-based), OR
    //   2. Reading is absolutely below 10ms (safety catch)
    water_now = (avg_time_us < dynamic_threshold) || (avg_time_us < RC_ABSOLUTE_MIN_THRESHOLD_US);
  #endif

  // Step 4: Slow baseline adaptation when DRY (prevents drift-induced false alarms)
  // Only update baseline if:
  //   - Currently in DRY state (not alerting)
  //   - Reading is valid (not timeout/disconnected)
  //   - It's been at least 10 seconds since last update
  //   - Reading is within reasonable dry range
  //   - Baseline hasn't been corrupted
  if (!water_detected && !water_now &&
      (millis() - baseline_update_time > 10000) &&
      avg_time_us > RC_ABSOLUTE_MIN_THRESHOLD_US &&
      avg_time_us < 150000 &&  // Reject timeout values (100-200ms)
      rc_baseline_us < 150000 && // Prevent using corrupted baseline
      baseline_established) {  // Only adapt after initial learning

    // Slow adaptation: move baseline 10% toward current reading
    // This allows it to track slow environmental drift without reacting to noise
    long delta = (long)avg_time_us - (long)rc_baseline_us;

    if (abs(delta) > 2000) {  // Only adapt if drift is >2ms
      // Use signed math to prevent unsigned underflow
      long new_baseline = (long)rc_baseline_us + (delta / 10);

      // Bounds check to prevent corruption
      if (new_baseline > 10000 && new_baseline < 150000) {
        rc_baseline_us = (unsigned long)new_baseline;
        baseline_update_time = millis();
        DEBUG_PRINT("Baseline adapted to: " + String(rc_baseline_us) + " us (drift detected)");
      }
    }
  }

  // Debouncing logic
  if (water_now && !water_detected) {
    // Water just appeared - start debounce timer
    if (water_detect_time == 0) {
      water_detect_time = millis();
      DEBUG_PRINT("Water detected - starting debounce timer");
    } else if (millis() - water_detect_time > WATER_DETECTION_DEBOUNCE_MS) {
      // Water consistently detected for debounce period
      water_detected = true;
      DEBUG_PRINT("DEBOUNCE COMPLETE - TRIGGERING ALERT!");
      return true;
    } else {
      unsigned long elapsed = millis() - water_detect_time;
      DEBUG_PRINT("Debouncing... " + String(elapsed) + "ms / " + String(WATER_DETECTION_DEBOUNCE_MS) + "ms");
    }
  } else if (!water_now && water_detected) {
    // Water disappeared - clear immediately (we want to know when it dries)
    water_detected = false;
    water_detect_time = 0;
    DEBUG_PRINT("Water cleared");
    return false;
  } else if (!water_now) {
    // Reset debounce timer if water not present
    if (water_detect_time != 0) {
      DEBUG_PRINT("Debounce reset - water reading fluctuated");
      water_detect_time = 0;
    }
  }

  return water_detected;
}

// ============================================
// Button Handling
// ============================================
void check_button() {
  bool button_state = (digitalRead(BUTTON_PIN_A) == LOW);  // Active low (pullup, button to GND)

  // Debounce
  if (button_state != button_pressed) {
    if (!button_debounce_flag) {
      button_debounce_time = millis();
      button_debounce_flag = true;
    } else if (millis() - button_debounce_time > BUTTON_DEBOUNCE_MS) {
      // Debounce passed
      button_pressed = button_state;
      button_debounce_flag = false;

      if (button_pressed) {
        button_press_start = millis();
        DEBUG_PRINT("Button pressed");
      } else {
        DEBUG_PRINT("Button released");
      }
    }
  } else {
    button_debounce_flag = false;
  }

  // Check for 1-second hold (only in ALERT state)
  if (button_pressed && system_state == STATE_ALERT) {
    if (millis() - button_press_start >= BUTTON_HOLD_TIME_MS) {
      DEBUG_PRINT("BUTTON HELD FOR 1 SECOND - ACKNOWLEDGING ALERT");
      set_system_state(STATE_STOPPED);
      button_pressed = false;  // Reset button state
    }
  }

  // Check for 2-second hold in STOPPED state to trigger reboot
  if (button_pressed && system_state == STATE_STOPPED) {
    if (millis() - button_press_start >= 2000) {  // 2 seconds
      DEBUG_PRINT("BUTTON HELD FOR 2 SECONDS - REBOOTING!");
      delay(100);  // Brief delay to let serial print
      NVIC_SystemReset();  // Software reset (ARM Cortex-M4 standard)
    }
  }
}

// ============================================
// Buzzer Control - Simplified Blocking Pattern
// ============================================
void update_buzzer() {
  if (system_state != STATE_ALERT) {
    // Turn off buzzer if not in alert mode
    digitalWrite(BUZZER_PIN_POSITIVE, LOW);
    digitalWrite(BUZZER_PIN_NEGATIVE, LOW);
    return;
  }

  // Alert mode - single loud beep every 2 seconds
  // Uses blocking square wave generation (same as startup beep) for maximum volume
  unsigned long now = millis();

  if (now - buzzer_last_beep >= (BUZZER_BEEP_DURATION_MS + BUZZER_BEEP_PAUSE_MS)) {
    // Time for a beep!
    buzzer_last_beep = now;

    // Generate blocking square wave at 4kHz for BUZZER_BEEP_DURATION_MS
    // At 4kHz, each cycle is 250us (125us high, 125us low)
    // For 200ms beep: 200,000us / 250us = 800 cycles
    int cycles = (BUZZER_BEEP_DURATION_MS * 1000) / 250;

    for (int i = 0; i < cycles; i++) {
      digitalWrite(BUZZER_PIN_POSITIVE, HIGH);
      digitalWrite(BUZZER_PIN_NEGATIVE, LOW);
      delayMicroseconds(125);  // 4kHz half-period
      digitalWrite(BUZZER_PIN_POSITIVE, LOW);
      digitalWrite(BUZZER_PIN_NEGATIVE, HIGH);
      delayMicroseconds(125);
    }

    // Turn off completely
    digitalWrite(BUZZER_PIN_POSITIVE, LOW);
    digitalWrite(BUZZER_PIN_NEGATIVE, LOW);

    DEBUG_PRINT("BEEP!");
  }
}

// ============================================
// System State Management
// ============================================
void set_system_state(uint8_t new_state) {
  if (new_state == system_state) return;

  DEBUG_PRINT("State transition: " + String(system_state) + " -> " + String(new_state));

  system_state = new_state;

  switch (new_state) {
    case STATE_NORMAL:
      current_flags = 0x00;  // leak=0, needs_ack=0
      set_advertising_mode(ADV_MODE_NORMAL);
      digitalWrite(BUZZER_PIN_POSITIVE, LOW);
      digitalWrite(BUZZER_PIN_NEGATIVE, LOW);
      DEBUG_PRINT("Entered NORMAL mode - monitoring for water");
      break;

    case STATE_ALERT:
      current_seq++;  // Increment sequence for new alert
      current_flags = 0x03;  // leak=1, needs_ack=1
      set_advertising_mode(ADV_MODE_ALERT);
      alert_start_time = millis();
      buzzer_last_beep = millis() - (BUZZER_BEEP_DURATION_MS + BUZZER_BEEP_PAUSE_MS);  // Beep immediately
      DEBUG_PRINT("!!! WATER DETECTED !!!");
      DEBUG_PRINT("Entered ALERT mode - sequence " + String(current_seq));
      break;

    case STATE_STOPPED:
      current_flags = 0x01;  // leak=1, needs_ack=0 (acknowledged)
      set_advertising_mode(ADV_MODE_NORMAL);
      digitalWrite(BUZZER_PIN_POSITIVE, LOW);
      digitalWrite(BUZZER_PIN_NEGATIVE, LOW);
      DEBUG_PRINT("Entered STOPPED mode - alert acknowledged");
      DEBUG_PRINT("Dry sensor, then hold button 2s to reboot");
      break;
  }
}

// ============================================
// BLE Setup
// ============================================
void setup_ble() {
  Bluefruit.begin();
  Bluefruit.setTxPower(0);  // 0dBm for battery efficiency
  Bluefruit.setName("LeakSeek");

  // Configure connection parameters for power savings
  Bluefruit.Periph.setConnIntervalMS(15, 30);  // 15-30ms
  Bluefruit.Periph.setConnSlaveLatency(5);  // Can skip 5 events
  Bluefruit.Periph.setConnSupervisionTimeout(4000);  // 4s timeout

  // Setup device information service
  bledis.setManufacturer("LeakSeek");
  bledis.setModel("nRF52-RC-v2.3");
  bledis.begin();

  // Setup custom service
  leakseek_service.begin();

  // Setup alert status characteristic (read + notify)
  leakseek_characteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  leakseek_characteristic.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  leakseek_characteristic.setFixedLen(1);
  leakseek_characteristic.begin();
  leakseek_characteristic.write8(0);

  // Setup ACK characteristic (write)
  ack_characteristic.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  ack_characteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  ack_characteristic.setFixedLen(1);
  ack_characteristic.setWriteCallback(ack_write_callback);
  ack_characteristic.begin();
  ack_characteristic.write8(0);

  DEBUG_PRINT("BLE services initialized");
}

// ============================================
// BLE Advertising
// ============================================
void set_advertising_mode(uint8_t mode) {
  adv_mode = mode;

  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();

  // Include standard fields
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();

  // Manufacturer data: 6 bytes (format matches Seeeduino library)
  // [0-1]: Manufacturer ID (little-endian)
  // [2]: Protocol version
  // [3]: Flags (bit0=leak, bit1=needs_ack)
  // [4]: Sequence number
  // [5]: Battery percent
  uint8_t adv_data[6];
  adv_data[0] = MANUFACTURER_ID & 0xFF;
  adv_data[1] = (MANUFACTURER_ID >> 8) & 0xFF;
  adv_data[2] = PROTOCOL_VERSION;
  adv_data[3] = current_flags;
  adv_data[4] = current_seq;
  adv_data[5] = battery_percent;
  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, adv_data, 6);

  // Set advertising interval and type based on mode
  if (mode == ADV_MODE_ALERT) {
    // Alert mode: connectable, fast advertising
    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED);
    if (millis() - alert_start_time < FAST_ADV_DURATION) {
      Bluefruit.Advertising.setInterval(ADV_INTERVAL_ALERT_FAST, ADV_INTERVAL_ALERT_FAST + 16);
    } else {
      Bluefruit.Advertising.setInterval(ADV_INTERVAL_ALERT_SLOW, ADV_INTERVAL_ALERT_SLOW + 16);
    }
    Bluefruit.Advertising.setFastTimeout(30);
  } else {
    // Normal mode: non-connectable, slow advertising
    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);
    Bluefruit.Advertising.setInterval(ADV_INTERVAL_NORMAL, ADV_INTERVAL_NORMAL);
  }

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0);  // Advertise forever

  DEBUG_PRINT("Advertising mode set to: " + String(mode == ADV_MODE_ALERT ? "ALERT" : "NORMAL"));
}

// ============================================
// ACK Write Callback
// ============================================
void ack_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
  if (len > 0 && data[0] == 0x01 && system_state == STATE_ALERT) {
    DEBUG_PRINT("ACK received via BLE - central acknowledged, but buzzer continues");
    DEBUG_PRINT("Press button to silence buzzer and freeze sensor");

    // Clear needs_ack flag but stay in ALERT state (keep buzzing!)
    current_flags = 0x01;  // leak=1, needs_ack=0
    set_advertising_mode(ADV_MODE_NORMAL);  // Return to normal advertising speed

    // Buzzer continues until button pressed - this is intentional!
    // The buzzer guides the user to the leak location.
  }
}

// ============================================
// Debug Output Functions
// ============================================
void print_water_debug() {
  #if DEBUG_MODE == 1
    // Text debug mode with adaptive threshold info
    unsigned long dynamic_threshold = (rc_baseline_us * (100 - RC_WATER_DROP_PERCENTAGE)) / 100;
    if (dynamic_threshold < RC_ABSOLUTE_MIN_THRESHOLD_US) {
      dynamic_threshold = RC_ABSOLUTE_MIN_THRESHOLD_US;
    }

    String status = (rc_time_us < dynamic_threshold) ? "WET" : "DRY";
    String state_name = (system_state == STATE_NORMAL) ? "NORMAL" :
                       (system_state == STATE_ALERT) ? "ALERT" : "STOPPED";

    Serial.print("RC: ");
    Serial.print(rc_time_us);
    Serial.print("us | Baseline: ");
    Serial.print(rc_baseline_us);
    Serial.print("us | Threshold: ");
    Serial.print(dynamic_threshold);
    Serial.print("us | Status: ");
    Serial.print(status);
    Serial.print(" | State: ");
    Serial.println(state_name);
  #endif
}

void print_graph_debug() {
  #if DEBUG_MODE == 2
    // Graph debug mode for Serial Plotter
    unsigned long current_time = measure_rc_time();

    // Calculate dynamic threshold
    unsigned long dynamic_threshold = (rc_baseline_us * (100 - RC_WATER_DROP_PERCENTAGE)) / 100;
    if (dynamic_threshold < RC_ABSOLUTE_MIN_THRESHOLD_US) {
      dynamic_threshold = RC_ABSOLUTE_MIN_THRESHOLD_US;
    }

    // Convert to milliseconds for easier reading (divide by 1000)
    float current_ms = current_time / 1000.0;
    float avg_ms = rc_time_us / 1000.0;
    float threshold_ms = dynamic_threshold / 1000.0;
    float baseline_ms = rc_baseline_us / 1000.0;

    // State value for visualization (scaled to fit nicely on graph)
    int state_value = 0;
    if (system_state == STATE_NORMAL) state_value = 0;
    else if (system_state == STATE_ALERT) state_value = 100;
    else if (system_state == STATE_STOPPED) state_value = 50;

    // Output format for Serial Plotter (space-separated Label:value)
    Serial.print("Raw:");
    Serial.print(current_ms, 2);
    Serial.print(" Avg:");
    Serial.print(avg_ms, 2);
    Serial.print(" Baseline:");
    Serial.print(baseline_ms, 2);
    Serial.print(" Threshold:");
    Serial.print(threshold_ms, 2);
    Serial.print(" State:");
    Serial.print(state_value);
    Serial.print(" AbsMin:");
    Serial.print(RC_ABSOLUTE_MIN_THRESHOLD_US / 1000.0, 2);
    Serial.print(" AbsMax:");
    Serial.println(RC_ABSOLUTE_MAX_DRY_US / 1000.0, 2);
  #endif
}

// ============================================
// LED Heartbeat
// ============================================
void update_led() {
  static unsigned long last_blink = 0;
  static bool led_state = false;
  unsigned long blink_interval;

  // Blink rate depends on system state
  if (system_state == STATE_NORMAL) {
    blink_interval = 2000;  // Slow blink - 2 seconds
  } else if (system_state == STATE_ALERT) {
    blink_interval = 100;   // Rapid blink - 100ms
  } else {
    blink_interval = 500;   // Medium blink - 500ms
  }

  if (millis() - last_blink > blink_interval) {
    led_state = !led_state;
    digitalWrite(LED_BUILTIN, led_state ? HIGH : LOW);
    last_blink = millis();
  }
}

// ============================================
// Utility Functions
// ============================================
void wait_for_serial(unsigned long timeout_ms) {
  unsigned long start = millis();
  while (!Serial && (millis() - start < timeout_ms)) {
    delay(10);
  }
}

// ============================================
// Setup
// ============================================
void setup() {
  Serial.begin(115200);
  wait_for_serial(2000);

  DEBUG_PRINT("\n========================================");
  DEBUG_PRINT("LeakSeek v2.3 - RC Timing Water Sensor");
  DEBUG_PRINT("Firmware Version: " FIRMWARE_VERSION);
  DEBUG_PRINT("========================================\n");

  // Configure LED for status indication
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // On during setup

  // Configure RC timing water sensor pins
  pinMode(WATER_SENSE_PIN_A, OUTPUT);
  pinMode(WATER_SENSE_PIN_B, INPUT);  // High-impedance input (NO pulldown!)
  digitalWrite(WATER_SENSE_PIN_A, LOW);
  DEBUG_PRINT("RC timing sensor configured on pins 7 & 8 (100nF cap on pin 8)");
  DEBUG_PRINT("Pin 8 set to high-impedance input (pulldown would prevent charging!)");
  DEBUG_PRINT("Expected dry time: 50-500ms, wet time: 0.5-5ms");

  // Initialize RC timing history buffer with reasonable dry values
  // This prevents false water detection during initial readings
  for (int i = 0; i < RC_SAMPLE_COUNT; i++) {
    rc_time_history[i] = 50000;  // Initialize to 50ms (typical dry state)
  }
  DEBUG_PRINT("RC timing history buffer initialized to 50ms (dry state)");
  DEBUG_PRINT("Baseline learning will begin - please keep sensor DRY for 2 seconds!");

  // Configure button pin with internal pullup (simpler hardware!)
  // Pin 0: Connected to button, other side to GND
  // When button pressed: Pin 0 pulled LOW (to GND)
  // When button released: Pin 0 pulled HIGH (internal pullup)
  pinMode(BUTTON_PIN_A, INPUT_PULLUP);  // Internal pullup resistor
  DEBUG_PRINT("Button configured: Pin 0 (INPUT_PULLUP), button to GND");
  DEBUG_PRINT("Button press pulls Pin 0 LOW");

  // Configure buzzer pins (OFF initially)
  pinMode(BUZZER_PIN_POSITIVE, OUTPUT);
  pinMode(BUZZER_PIN_NEGATIVE, OUTPUT);
  digitalWrite(BUZZER_PIN_POSITIVE, LOW);
  digitalWrite(BUZZER_PIN_NEGATIVE, LOW);
  DEBUG_PRINT("Buzzer configured on pins 6 & 4");

  // Test buzzer at startup (short beep)
  DEBUG_PRINT("Testing buzzer...");
  for (int i = 0; i < 400; i++) {  // 400 cycles at 4kHz = 100ms beep
    digitalWrite(BUZZER_PIN_POSITIVE, HIGH);
    digitalWrite(BUZZER_PIN_NEGATIVE, LOW);
    delayMicroseconds(125);  // 4kHz half-period
    digitalWrite(BUZZER_PIN_POSITIVE, LOW);
    digitalWrite(BUZZER_PIN_NEGATIVE, HIGH);
    delayMicroseconds(125);
  }
  digitalWrite(BUZZER_PIN_POSITIVE, LOW);
  digitalWrite(BUZZER_PIN_NEGATIVE, LOW);
  DEBUG_PRINT("Buzzer test complete - did you hear a beep?");

  // Setup BLE
  setup_ble();

  // Start advertising
  set_advertising_mode(ADV_MODE_NORMAL);

  // Initialize system state
  set_system_state(STATE_NORMAL);

  // Hardware watchdog (production only)
  #if !DEBUG
    NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos) |
                      (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos);
    NRF_WDT->CRV = 32768 * WDT_TIMEOUT_SECONDS;
    NRF_WDT->RREN |= WDT_RREN_RR0_Msk;
    NRF_WDT->TASKS_START = 1;
    DEBUG_PRINT("Hardware watchdog enabled (4s timeout)");
  #endif

  DEBUG_PRINT("\nInitialization complete - starting baseline learning");
  DEBUG_PRINT("Adaptive threshold: " + String(RC_WATER_DROP_PERCENTAGE) + "% drop from baseline");
  DEBUG_PRINT("Absolute minimum threshold: " + String(RC_ABSOLUTE_MIN_THRESHOLD_US) + " us (" + String(RC_ABSOLUTE_MIN_THRESHOLD_US / 1000.0) + " ms)");
  DEBUG_PRINT("Keep sensor DRY for next 2 seconds to establish baseline!");
  DEBUG_PRINT("========================================\n");

  digitalWrite(LED_BUILTIN, LOW);
}

// ============================================
// Main Loop
// ============================================
void loop() {
  // Feed watchdog (production only)
  #if !DEBUG
    NRF_WDT->RR[0] = WDT_RR_RR_Reload;
  #endif

  // State machine logic (with non-blocking sensor timing)
  static unsigned long last_sensor_check = 0;
  if (millis() - last_sensor_check >= LOOP_DELAY_MS) {
    last_sensor_check = millis();

    if (system_state == STATE_NORMAL) {
      // Monitor for water detection
      if (check_water_detected()) {
        set_system_state(STATE_ALERT);
      }
    } else if (system_state == STATE_ALERT) {
      // Continue checking water status
      check_water_detected();

      // Update advertising if we've passed fast advertising duration
      if (millis() - alert_start_time > FAST_ADV_DURATION &&
          adv_mode == ADV_MODE_ALERT) {
        set_advertising_mode(ADV_MODE_ALERT);  // Will set to slow interval
      }
    } else if (system_state == STATE_STOPPED) {
      // Stopped - waiting for reboot
      // Still check water status for debug purposes
      check_water_detected();
    }
  }

  // Always check button and update buzzer
  check_button();
  update_buzzer();
  update_led();

  // Debug output
  static unsigned long last_debug = 0;
  static unsigned long last_graph = 0;

  #if DEBUG_MODE == 1
    if (millis() - last_debug > 2000) {
      print_water_debug();
      last_debug = millis();
    }
  #elif DEBUG_MODE == 2
    if (millis() - last_graph > GRAPH_DEBUG_INTERVAL_MS) {
      print_graph_debug();
      last_graph = millis();
    }
  #endif

  // No delay! Need to run fast for buzzer square wave generation
  // Sensor checking is handled by non-blocking timing in state machine

  loop_iterations++;
  last_loop_time = millis();
}
