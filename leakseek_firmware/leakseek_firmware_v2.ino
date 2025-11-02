// LeakSeek Firmware v2.1 - Advertisement-based monitoring with enhanced reliability
// See: https://github.com/adafruit/Adafruit_nRF52_Arduino/tree/master/libraries/Bluefruit52Lib/src
#define DEBUG true
#define FIRMWARE_VERSION "2.1.0"

#include <bluefruit.h>
#include <string.h>
#include "config.h"

// Global state
uint8_t current_seq = 0;
uint8_t current_flags = 0;  // bit0=leak, bit1=needs_ack
uint8_t battery_percent = 100;
uint8_t adv_mode = ADV_MODE_NORMAL;
unsigned long alert_start_time = 0;
const unsigned long FAST_ADV_DURATION = 5000; // 5 seconds of fast advertising

// Watchdog and reliability
unsigned long last_loop_time = 0;
unsigned long loop_iterations = 0;
bool is_first_boot = true;

// Configure our service and characteristics
BLEService leakseek_service = BLEService(SERVICE_UUID);
BLECharacteristic leakseek_characteristic = BLECharacteristic(CHARACTERISTIC_UUID);
BLECharacteristic ack_characteristic = BLECharacteristic(ACK_CHARACTERISTIC_UUID);
BLEDis bledis;

// Forward declarations
void set_advertising_mode(uint8_t mode);
void update_manufacturer_data();
void ack_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len);

void setup() {
  Serial.begin(115200);
  wait_for_serial(2000);

  DEBUG_PRINT("LeakSeek v2.1 - Advertisement-based monitoring (Enhanced)");
  DEBUG_PRINT("Firmware Version: " FIRMWARE_VERSION);
  DEBUG_PRINT("---------------------------------------------\n");

  // Configure LED for status indication
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // On during setup

  // Configure leak sensor pin with internal pullup
  // Pin reads HIGH normally, LOW when button pressed (connected to GND)
  pinMode(LEAK_SENSOR_PIN, INPUT_PULLUP);
  DEBUG_PRINT("Leak sensor pin configured on D9");

  // Enable watchdog timer (4 seconds timeout for safety)
  // This will reset the device if it hangs
  #ifndef DEBUG
  // Only enable watchdog in production (not during debug/development)
  Watchdog.enable(4000);
  DEBUG_PRINT("Watchdog timer enabled (4s timeout)");
  #else
  DEBUG_PRINT("Watchdog timer DISABLED (debug mode)");
  #endif

  // Start the BLE module with optimized settings
  Bluefruit.begin(1, 0);
  Bluefruit.setTxPower(0);  // Reduced from +4dBm to 0dBm for better power efficiency
  bond_clear_all(); // Clear all bonds for testing

  setup_device_and_device_information();
  setup_service();
  setup_peripheral();

  // Start in normal mode (non-connectable)
  set_advertising_mode(ADV_MODE_NORMAL);

  digitalWrite(LED_BUILTIN, LOW); // Off after setup complete
  is_first_boot = false;

  DEBUG_PRINT("Setup complete. Waiting for events...");
}

void loop() {
  static uint8_t last_seq = 0xFF;
  static uint8_t last_flags = 0xFF;
  static uint8_t last_battery = 0xFF;
  static int8_t last_indicated_state = -1;
  static unsigned long last_incident_time = 0;

  // Reset watchdog timer to prevent reset
  #ifndef DEBUG
  Watchdog.reset();
  #endif

  // Track loop iterations for diagnostics
  loop_iterations++;
  last_loop_time = millis();

  // LED indication: different patterns for different states
  static unsigned long last_led_toggle = 0;
  unsigned long led_interval;

  if (current_flags & 0x02) {
    // Alert mode: rapid blink (100ms)
    led_interval = 100;
  } else if (Bluefruit.connected()) {
    // Connected: medium blink (500ms)
    led_interval = 500;
  } else {
    // Normal: slow blink (2000ms) for heartbeat
    led_interval = 2000;
  }

  if (millis() - last_led_toggle > led_interval) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    last_led_toggle = millis();
  }

  // Simulate leak detection for demo with cooldown to prevent duplicates
  // For real sensor: uint8_t sensor_state = (digitalRead(LEAK_SENSOR_PIN) == LOW) ? 1 : 0;
  uint8_t sensor_state = 0;
  
  // Only allow new incidents if cooldown period has passed
  if (millis() - last_incident_time > INCIDENT_COOLDOWN_MS) {
    if (random(0, 100) < LEAK_CHANCE_PERCENT) {
      sensor_state = 1;
      DEBUG_PRINT("Simulated leak triggered!");
    }
  }

  // Check if we should transition modes
  if (sensor_state == 1 && !(current_flags & 0x01)) {
    // New leak detected
    DEBUG_PRINT("LEAK DETECTED!");
    current_seq++;
    current_flags = 0x03; // leak=1, needs_ack=1
    set_advertising_mode(ADV_MODE_ALERT);
    alert_start_time = millis();
    last_incident_time = millis(); // Record incident time for cooldown
  }
  
  // If in alert mode and fast advertising period expired, slow down
  if (adv_mode == ADV_MODE_ALERT && 
      (current_flags & 0x02) && 
      (millis() - alert_start_time > FAST_ADV_DURATION)) {
    // Switch to slower but still connectable advertising
    Bluefruit.Advertising.stop();
    Bluefruit.Advertising.setInterval(ADV_INTERVAL_ALERT_SLOW, ADV_INTERVAL_ALERT_SLOW + 84);
    Bluefruit.Advertising.start(0);
    DEBUG_PRINT("Switched to slower alert advertising");
  }

  // Update battery reading only every 60 seconds to reduce advertising rebuilds
  static unsigned long last_battery_update = 0;
  if (millis() - last_battery_update > 60000) {
    battery_percent = 100 - (millis() / 600000) % 100; // Very slow drain for demo
    last_battery_update = millis();
  }

  // Rebuild advertising ONLY when manufacturer data changes
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

  // Handle connection-based indication (only send when state changes)
  if (Bluefruit.connected()) {
    uint8_t state = (current_flags & 0x01) ? 1 : 0;
    if (state != last_indicated_state) {
      uint16_t conn_handle = Bluefruit.connHandle();
      leakseek_characteristic.indicate8(conn_handle, state);
      DEBUG_PRINT("Connected - Sent indication: ");
      DEBUG_PRINT(state);
      last_indicated_state = state;
    }
  } else {
    last_indicated_state = -1; // Reset when disconnected
  }

  delay(LOOP_DELAY_MS);
}

void setup_device_and_device_information() {
  Bluefruit.setName("LeakSeek");
  // TX power already set in setup() for power efficiency

  bledis.setManufacturer("Kozak Industries");
  bledis.setModel("LeakSeek Drop v2.1");
  bledis.setFirmwareRev(FIRMWARE_VERSION);
  bledis.begin();

  DEBUG_PRINT("Device name: LeakSeek");
  DEBUG_PRINT("Manufacturer: Kozak Industries");
  DEBUG_PRINT("Model: LeakSeek Drop v2.1");
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

  // New ACK characteristic for clearing alerts
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
  // Connection interval: 1000-1100ms (slower = less power)
  // Slave latency: 5 (can skip 5 connection events to save power)
  // Supervision timeout: 6000ms (allow disconnection detection)
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
    // Normal mode: non-connectable, slow advertising
    DEBUG_PRINT("Setting NORMAL mode (non-connectable)");
    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);
    Bluefruit.Advertising.setInterval(ADV_INTERVAL_NORMAL, ADV_INTERVAL_NORMAL);
  } else {
    // Alert mode: connectable, fast advertising initially
    DEBUG_PRINT("Setting ALERT mode (connectable, fast)");
    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED);
    Bluefruit.Advertising.setInterval(ADV_INTERVAL_ALERT_FAST, ADV_INTERVAL_ALERT_FAST + 16);
    Bluefruit.Advertising.setFastTimeout(30);
  }
  
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0);
  
  DEBUG_PRINT("Advertising started");
}

// Note: update_manufacturer_data() removed - now handled inline in loop()
// to properly rebuild advertising when data changes

void ack_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
  DEBUG_PRINT("ACK write callback");
  
  if (len == 1) {
    uint8_t received_seq = data[0];
    DEBUG_PRINT("Received ACK for seq: ");
    DEBUG_PRINT(received_seq);
    
    // If sequence matches, clear the alert
    if (received_seq == current_seq) {
      DEBUG_PRINT("ACK confirmed - clearing alert");
      current_flags = 0x00; // Clear all flags
      
      // Switch back to normal mode
      set_advertising_mode(ADV_MODE_NORMAL);
      
      // Update the ACK characteristic value
      ack_characteristic.write8(current_seq);
    } else {
      DEBUG_PRINT("ACK sequence mismatch");
    }
  }
}

void connect_callback(uint16_t conn_handle) {
  BLEConnection* connection = Bluefruit.Connection(conn_handle);

  if (connection) {
    char central_name[32] = { 0 };
    connection->getPeerName(central_name, sizeof(central_name));

    DEBUG_PRINT("Connected to: ");
    DEBUG_PRINT(central_name);
  } else {
    DEBUG_PRINT("Connected (peer name unavailable)");
  }
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void) conn_handle;

  DEBUG_PRINT("Disconnected, reason = 0x");
  DEBUG_PRINT(reason, HEX);

  // Common disconnect reasons:
  // 0x13 = Remote User Terminated Connection
  // 0x16 = Connection Terminated by Local Host
  // 0x08 = Connection Timeout
  // 0x3E = Connection Failed to be Established

  // If still in alert mode, keep advertising
  if (current_flags & 0x02) {
    DEBUG_PRINT("Still need ACK - continuing alert advertising");
  } else {
    DEBUG_PRINT("Disconnect after successful ACK - returning to normal mode");
  }
}

void wait_for_serial(int time) {
  unsigned long serial_wait_start = millis();
  while (!Serial && (millis() - serial_wait_start < time)) {
    delay(10);
  }
}
