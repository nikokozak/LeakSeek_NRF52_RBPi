// LeakSeek Firmware v2.2 - Water Sensor Implementation for XIAO nRF52 Custom PCB
// Hardware: CR2032 powered, ITO trace water sensor, button, buzzer
// See: https://github.com/adafruit/Adafruit_nRF52_Arduino/tree/master/libraries/Bluefruit52Lib/src

#define DEBUG true
#define FIRMWARE_VERSION "2.2.0-water-sensor"

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

// Water detection state
int water_threshold = WATER_THRESHOLD_DEFAULT;
bool water_detected = false;
unsigned long water_detect_time = 0;
int water_reading_history[WATER_SAMPLE_COUNT] = {0};
int water_history_index = 0;

// Button state
bool button_pressed = false;
unsigned long button_press_start = 0;
bool button_debounce_flag = false;
unsigned long button_debounce_time = 0;

// Buzzer state
unsigned long buzzer_last_beep = 0;
int buzzer_beep_count = 0;
bool buzzer_on = false;
unsigned long buzzer_sequence_start = 0;

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

// ============================================
// Forward Declarations
// ============================================
void set_advertising_mode(uint8_t mode);
void ack_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len);
int read_water_sensor();
bool check_water_detected();
void check_button();
void update_buzzer();
void set_system_state(uint8_t new_state);
void print_water_debug();
void print_graph_debug();

// ============================================
// Setup
// ============================================
void setup() {
  Serial.begin(115200);
  wait_for_serial(2000);

  DEBUG_PRINT("\n========================================");
  DEBUG_PRINT("LeakSeek v2.2 - Water Sensor Edition");
  DEBUG_PRINT("Firmware Version: " FIRMWARE_VERSION);
  DEBUG_PRINT("========================================\n");

  // Configure LED for status indication
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // On during setup

  // Configure water sensor pins
  pinMode(WATER_SENSE_PIN_A, INPUT);      // Analog input for sensing
  pinMode(WATER_SENSE_PIN_B, OUTPUT);     // Drive pin (has 100nF cap to GND)
  digitalWrite(WATER_SENSE_PIN_B, HIGH);  // Pull high for resistance measurement
  DEBUG_PRINT("Water sensor configured on pins 7 & 8");

  // Configure button pins (using internal pullup for safety)
  pinMode(BUTTON_PIN_A, INPUT_PULLUP);
  pinMode(BUTTON_PIN_B, OUTPUT);
  digitalWrite(BUTTON_PIN_B, LOW);  // Button connects to GND
  DEBUG_PRINT("Button configured on pins 0 & 1");

  // Configure buzzer pins (OFF initially)
  pinMode(BUZZER_PIN_POSITIVE, OUTPUT);
  pinMode(BUZZER_PIN_NEGATIVE, OUTPUT);
  digitalWrite(BUZZER_PIN_POSITIVE, LOW);
  digitalWrite(BUZZER_PIN_NEGATIVE, LOW);
  DEBUG_PRINT("Buzzer configured on pins 5 & 6");

  // Enable watchdog timer (4 seconds timeout for safety)
  #ifndef DEBUG
  Watchdog.enable(4000);
  DEBUG_PRINT("Watchdog timer enabled (4s timeout)");
  #else
  DEBUG_PRINT("Watchdog timer DISABLED (debug mode)");
  #endif

  // Start the BLE module with optimized settings
  Bluefruit.begin(1, 0);
  Bluefruit.setTxPower(0);  // 0dBm for power efficiency
  Bluefruit.setName("LeakSeek");

  setup_device_information();
  setup_service();
  setup_peripheral();

  // Start in normal mode (non-connectable)
  set_advertising_mode(ADV_MODE_NORMAL);

  digitalWrite(LED_BUILTIN, LOW); // Off after setup complete

  #if DEBUG_MODE == 1
    DEBUG_PRINT("\n========================================");
    DEBUG_PRINT("Setup complete. System in NORMAL mode.");
    DEBUG_PRINT("Water threshold: " + String(water_threshold));
    DEBUG_PRINT("Monitoring for water...");
    DEBUG_PRINT("========================================\n");
  #elif DEBUG_MODE == 2
    Serial.println("\n========================================");
    Serial.println("LeakSeek Water Sensor - GRAPH DEBUG MODE");
    Serial.println("Open Tools > Serial Plotter");
    Serial.print("Threshold: ");
    Serial.println(water_threshold);
    Serial.println("========================================\n");
    delay(2000);  // Give user time to open plotter
  #endif

  // Beep once to confirm system is ready
  digitalWrite(BUZZER_PIN_POSITIVE, HIGH);
  delay(50);
  digitalWrite(BUZZER_PIN_POSITIVE, LOW);
}

// ============================================
// Main Loop
// ============================================
void loop() {
  static uint8_t last_seq = 0xFF;
  static uint8_t last_flags = 0xFF;
  static uint8_t last_battery = 0xFF;
  static int8_t last_indicated_state = -1;
  static unsigned long last_debug_print = 0;

  // Reset watchdog timer to prevent reset
  #ifndef DEBUG
  Watchdog.reset();
  #endif

  // Track loop iterations for diagnostics
  loop_iterations++;
  last_loop_time = millis();

  // LED indication based on system state
  static unsigned long last_led_toggle = 0;
  unsigned long led_interval;

  switch (system_state) {
    case STATE_NORMAL:
      led_interval = 2000;  // Slow blink - heartbeat
      break;
    case STATE_ALERT:
      led_interval = 100;   // Rapid blink - alert
      break;
    case STATE_STOPPED:
      led_interval = 500;   // Medium blink - stopped
      break;
  }

  if (millis() - last_led_toggle > led_interval) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    last_led_toggle = millis();
  }

  // ========================================
  // State Machine Logic
  // ========================================

  switch (system_state) {
    case STATE_NORMAL:
      // Check for water detection
      if (WATER_DETECTION_ENABLED && check_water_detected()) {
        DEBUG_PRINT("\n!!! WATER DETECTED !!!");
        DEBUG_PRINT("Transitioning to ALERT mode");
        set_system_state(STATE_ALERT);
      }
      break;

    case STATE_ALERT:
      // Update buzzer (3-beep pattern)
      update_buzzer();

      // Check button for 1-second hold
      check_button();

      // If button held for 1 second, transition to STOPPED
      if (button_pressed && (millis() - button_press_start >= BUTTON_HOLD_TIME_MS)) {
        DEBUG_PRINT("\n!!! BUTTON HELD FOR 1 SECOND !!!");
        DEBUG_PRINT("Transitioning to STOPPED mode");
        set_system_state(STATE_STOPPED);
      }
      break;

    case STATE_STOPPED:
      // Do nothing - system is stopped, waiting for reboot
      // User must dry sensor and reboot device
      break;
  }

  // ========================================
  // Serial Debug Output
  // ========================================
  #if DEBUG_MODE == 1
    // Text debug (every 2 seconds in NORMAL mode)
    if (system_state == STATE_NORMAL && (millis() - last_debug_print > 2000)) {
      print_water_debug();
      last_debug_print = millis();
    }
  #elif DEBUG_MODE == 2
    // Graph debug (fast updates for Serial Plotter)
    static unsigned long last_graph_update = 0;
    if (millis() - last_graph_update > GRAPH_DEBUG_INTERVAL_MS) {
      print_graph_debug();
      last_graph_update = millis();
    }
  #endif

  // ========================================
  // Update Battery Reading (every 60 seconds)
  // ========================================
  static unsigned long last_battery_update = 0;
  if (millis() - last_battery_update > 60000) {
    // TODO: Implement actual battery voltage reading via analog pin
    // For now, simulate slow drain
    battery_percent = max(0, battery_percent - 1);
    last_battery_update = millis();
  }

  // ========================================
  // Update BLE Advertisement (only when data changes)
  // ========================================
  if (current_seq != last_seq || current_flags != last_flags || battery_percent != last_battery) {
    DEBUG_PRINT("Updating manufacturer data in advertisement");

    // Stop and clear current advertising
    Bluefruit.Advertising.stop();
    Bluefruit.Advertising.clearData();

    // Rebuild advertising packet
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(leakseek_service);
    Bluefruit.Advertising.addName();

    // Add manufacturer data
    uint8_t adv_data[6];
    adv_data[0] = MANUFACTURER_ID & 0xFF;
    adv_data[1] = (MANUFACTURER_ID >> 8) & 0xFF;
    adv_data[2] = PROTOCOL_VERSION;
    adv_data[3] = current_flags;
    adv_data[4] = current_seq;
    adv_data[5] = battery_percent;

    Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, adv_data, 6);

    // Restart advertising
    Bluefruit.Advertising.start(0);

    // Update tracking
    last_seq = current_seq;
    last_flags = current_flags;
    last_battery = battery_percent;
  }

  // ========================================
  // Handle Connection-Based Indication
  // ========================================
  if (Bluefruit.connected()) {
    uint8_t state = (current_flags & 0x01) ? 1 : 0;
    if (state != last_indicated_state) {
      uint16_t conn_handle = Bluefruit.connHandle();
      leakseek_characteristic.indicate8(conn_handle, state);
      DEBUG_PRINT("Connected - Sent indication: " + String(state));
      last_indicated_state = state;
    }
  } else {
    last_indicated_state = -1; // Reset when disconnected
  }

  delay(LOOP_DELAY_MS);
}

// ============================================
// Water Sensor Functions
// ============================================

int read_water_sensor() {
  // Read analog value from water sensor
  // Lower value = more conductive = water present
  // Higher value = less conductive = dry

  int reading = analogRead(WATER_SENSE_PIN_A);

  // Add to rolling average
  water_reading_history[water_history_index] = reading;
  water_history_index = (water_history_index + 1) % WATER_SAMPLE_COUNT;

  // Calculate average
  int sum = 0;
  for (int i = 0; i < WATER_SAMPLE_COUNT; i++) {
    sum += water_reading_history[i];
  }
  return sum / WATER_SAMPLE_COUNT;
}

bool check_water_detected() {
  int avg_reading = read_water_sensor();

  // Water detected if reading is BELOW threshold (more conductive)
  bool water_now = (avg_reading < water_threshold);

  // Debounce: water must be detected continuously for WATER_DETECTION_DEBOUNCE_MS
  if (water_now && !water_detected) {
    if (water_detect_time == 0) {
      water_detect_time = millis();
    } else if (millis() - water_detect_time >= WATER_DETECTION_DEBOUNCE_MS) {
      water_detected = true;
      return true;
    }
  } else if (!water_now) {
    water_detect_time = 0;  // Reset debounce timer
  }

  return false;
}

void print_water_debug() {
  int avg_reading = read_water_sensor();

  Serial.print("Water Sensor: ");
  Serial.print(avg_reading);
  Serial.print(" | Threshold: ");
  Serial.print(water_threshold);
  Serial.print(" | Status: ");
  Serial.print(avg_reading < water_threshold ? "WET" : "DRY");
  Serial.print(" | State: ");
  Serial.println(system_state == STATE_NORMAL ? "NORMAL" :
                 system_state == STATE_ALERT ? "ALERT" : "STOPPED");
}

void print_graph_debug() {
  // Output format optimized for Arduino Serial Plotter
  // Format: "Label1:value1 Label2:value2 Label3:value3"
  // This creates multiple traces on the plotter

  int current_reading = analogRead(WATER_SENSE_PIN_A);  // Raw reading
  int avg_reading = read_water_sensor();                 // Averaged reading

  // Output in Serial Plotter format (space-separated, with labels)
  Serial.print("Raw:");
  Serial.print(current_reading);
  Serial.print(" ");

  Serial.print("Avg:");
  Serial.print(avg_reading);
  Serial.print(" ");

  Serial.print("Threshold:");
  Serial.print(water_threshold);
  Serial.print(" ");

  // Add state indicator (scaled to fit on graph)
  // 0 = NORMAL, 300 = ALERT, 150 = STOPPED
  int state_value = (system_state == STATE_NORMAL) ? 0 :
                    (system_state == STATE_ALERT) ? 300 : 150;
  Serial.print("State:");
  Serial.print(state_value);
  Serial.print(" ");

  // Add threshold +/- margins for visual reference
  Serial.print("Upper:");
  Serial.print(water_threshold + 100);
  Serial.print(" ");

  Serial.print("Lower:");
  Serial.println(water_threshold - 100);
}

// ============================================
// Button Functions
// ============================================

void check_button() {
  bool button_state = (digitalRead(BUTTON_PIN_A) == LOW);  // LOW = pressed

  // Debounce logic
  if (button_state && !button_debounce_flag) {
    if (button_debounce_time == 0) {
      button_debounce_time = millis();
    } else if (millis() - button_debounce_time >= BUTTON_DEBOUNCE_MS) {
      // Button confirmed pressed
      if (!button_pressed) {
        button_pressed = true;
        button_press_start = millis();
        DEBUG_PRINT("Button pressed - Hold for 1 second to acknowledge");
      }
      button_debounce_flag = true;
    }
  } else if (!button_state) {
    // Button released
    if (button_pressed) {
      unsigned long hold_time = millis() - button_press_start;
      DEBUG_PRINT("Button released after " + String(hold_time) + "ms");
    }
    button_pressed = false;
    button_press_start = 0;
    button_debounce_flag = false;
    button_debounce_time = 0;
  }
}

// ============================================
// Buzzer Functions
// ============================================

void update_buzzer() {
  unsigned long current_time = millis();
  unsigned long time_in_sequence = current_time - buzzer_sequence_start;

  // Calculate timing for 3-beep pattern:
  // BEEP1 (100ms) -> PAUSE (100ms) -> BEEP2 (100ms) -> PAUSE (100ms) -> BEEP3 (100ms) -> LONG PAUSE (1000ms)
  // Total cycle: 100 + 100 + 100 + 100 + 100 + 1000 = 1500ms

  int cycle_position = time_in_sequence % 1500;

  // Beep 1: 0-100ms
  // Pause 1: 100-200ms
  // Beep 2: 200-300ms
  // Pause 2: 300-400ms
  // Beep 3: 400-500ms
  // Long pause: 500-1500ms

  bool should_beep = false;

  if (cycle_position < 100) {
    should_beep = true;  // Beep 1
  } else if (cycle_position >= 200 && cycle_position < 300) {
    should_beep = true;  // Beep 2
  } else if (cycle_position >= 400 && cycle_position < 500) {
    should_beep = true;  // Beep 3
  }

  // Update buzzer state
  if (should_beep && !buzzer_on) {
    digitalWrite(BUZZER_PIN_POSITIVE, HIGH);
    buzzer_on = true;
  } else if (!should_beep && buzzer_on) {
    digitalWrite(BUZZER_PIN_POSITIVE, LOW);
    buzzer_on = false;
  }
}

// ============================================
// State Machine Functions
// ============================================

void set_system_state(uint8_t new_state) {
  uint8_t old_state = system_state;
  system_state = new_state;

  switch (new_state) {
    case STATE_NORMAL:
      // Reset flags and modes
      current_flags = 0x00;
      set_advertising_mode(ADV_MODE_NORMAL);

      // Turn off buzzer
      digitalWrite(BUZZER_PIN_POSITIVE, LOW);
      digitalWrite(BUZZER_PIN_NEGATIVE, LOW);
      buzzer_on = false;

      DEBUG_PRINT("System state: NORMAL");
      break;

    case STATE_ALERT:
      // Set alert flags
      current_seq++;
      current_flags = 0x03;  // leak=1, needs_ack=1
      set_advertising_mode(ADV_MODE_ALERT);
      alert_start_time = millis();

      // Start buzzer sequence
      buzzer_sequence_start = millis();

      DEBUG_PRINT("System state: ALERT");
      DEBUG_PRINT("Sequence: " + String(current_seq));
      DEBUG_PRINT("Buzzer started");
      break;

    case STATE_STOPPED:
      // Clear needs_ack flag (keep leak flag)
      current_flags = 0x01;  // leak=1, needs_ack=0
      set_advertising_mode(ADV_MODE_NORMAL);

      // Turn off buzzer
      digitalWrite(BUZZER_PIN_POSITIVE, LOW);
      digitalWrite(BUZZER_PIN_NEGATIVE, LOW);
      buzzer_on = false;

      DEBUG_PRINT("System state: STOPPED");
      DEBUG_PRINT("System acknowledged. Dry sensor and reboot to reset.");
      break;
  }
}

// ============================================
// BLE Setup Functions
// ============================================

void setup_device_information() {
  BLEDis bledis;
  bledis.setManufacturer("Kozak Industries");
  bledis.setModel("LeakSeek Drop v2.2 (Water Sensor)");
  bledis.setFirmwareRev(FIRMWARE_VERSION);
  bledis.begin();

  DEBUG_PRINT("Device name: LeakSeek");
  DEBUG_PRINT("Manufacturer: Kozak Industries");
  DEBUG_PRINT("Model: LeakSeek Drop v2.2");
}

void setup_service() {
  // Configure the service
  leakseek_service.begin();

  // Original characteristic for reading and indications
  leakseek_characteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_INDICATE);
  leakseek_characteristic.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  leakseek_characteristic.setFixedLen(1);
  leakseek_characteristic.begin();
  leakseek_characteristic.write8(0);

  // ACK characteristic for clearing alerts
  ack_characteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE_WO_RESP);
  ack_characteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  ack_characteristic.setFixedLen(1);
  ack_characteristic.setWriteCallback(ack_write_callback);
  ack_characteristic.begin();
  ack_characteristic.write8(0);
}

void setup_peripheral() {
  Bluefruit.Periph.begin();

  // Optimized connection parameters for power efficiency
  Bluefruit.Periph.setConnIntervalMS(1000, 1100);
  Bluefruit.Periph.setConnSlaveLatency(5);
  Bluefruit.Periph.setConnSupervisionTimeout(6000);

  // Set callbacks
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  DEBUG_PRINT("Peripheral configured with power-optimized connection parameters");
}

void set_advertising_mode(uint8_t mode) {
  adv_mode = mode;

  // Stop any existing advertising
  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();

  // Common elements
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(leakseek_service);
  Bluefruit.Advertising.addName();

  // Add manufacturer data
  uint8_t adv_data[6];
  adv_data[0] = MANUFACTURER_ID & 0xFF;
  adv_data[1] = (MANUFACTURER_ID >> 8) & 0xFF;
  adv_data[2] = PROTOCOL_VERSION;
  adv_data[3] = current_flags;
  adv_data[4] = current_seq;
  adv_data[5] = battery_percent;
  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, adv_data, 6);

  if (mode == ADV_MODE_NORMAL) {
    DEBUG_PRINT("Setting NORMAL advertising mode (non-connectable)");
    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);
    Bluefruit.Advertising.setInterval(ADV_INTERVAL_NORMAL, ADV_INTERVAL_NORMAL);
  } else {
    DEBUG_PRINT("Setting ALERT advertising mode (connectable, fast)");
    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED);
    Bluefruit.Advertising.setInterval(ADV_INTERVAL_ALERT_FAST, ADV_INTERVAL_ALERT_FAST + 16);
    Bluefruit.Advertising.setFastTimeout(30);
  }

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0);

  DEBUG_PRINT("Advertising started");
}

// ============================================
// BLE Callback Functions
// ============================================

void ack_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
  DEBUG_PRINT("ACK write callback");

  if (len == 1) {
    uint8_t received_seq = data[0];
    DEBUG_PRINT("Received ACK for seq: " + String(received_seq));

    // If sequence matches, transition to STOPPED state
    if (received_seq == current_seq && system_state == STATE_ALERT) {
      DEBUG_PRINT("ACK confirmed - transitioning to STOPPED");
      set_system_state(STATE_STOPPED);

      // Update the ACK characteristic value
      ack_characteristic.write8(current_seq);
    } else {
      DEBUG_PRINT("ACK sequence mismatch or wrong state");
    }
  }
}

void connect_callback(uint16_t conn_handle) {
  BLEConnection* connection = Bluefruit.Connection(conn_handle);

  if (connection) {
    char central_name[32] = { 0 };
    connection->getPeerName(central_name, sizeof(central_name));
    DEBUG_PRINT("Connected to: " + String(central_name));
  } else {
    DEBUG_PRINT("Connected (peer name unavailable)");
  }
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void) conn_handle;

  DEBUG_PRINT("Disconnected, reason = 0x" + String(reason, HEX));

  if (system_state == STATE_ALERT) {
    DEBUG_PRINT("Still in ALERT mode - continuing advertising");
  } else if (system_state == STATE_STOPPED) {
    DEBUG_PRINT("Disconnect after acknowledgment");
  }
}

// ============================================
// Utility Functions
// ============================================

void wait_for_serial(int time) {
  unsigned long serial_wait_start = millis();
  while (!Serial && (millis() - serial_wait_start < time)) {
    delay(10);
  }
}
