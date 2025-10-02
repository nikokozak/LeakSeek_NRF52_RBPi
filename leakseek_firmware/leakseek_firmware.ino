// See: https://github.com/adafruit/Adafruit_nRF52_Arduino/tree/master/libraries/Bluefruit52Lib/src
#define DEBUG t

#include <bluefruit.h>
#include "config.h"

// Configure our service and characteristic
BLEService leakseek_service = BLEService(SERVICE_UUID);
BLECharacteristic leakseek_characteristic = BLECharacteristic(CHARACTERISTIC_UUID);
// DIS (Device Information Service) helper class instance
BLEDis bledis;    


void setup() {

  Serial.begin(115200);
  wait_for_serial(2000); // Wait for serial startup, continue regardless.

  DEBUG_PRINT("LeakSeek debug console");
  DEBUG_PRINT("-----------------------\n");

  // Start the BLE module
  // Max conns as Periph, Max conns as Central
  Bluefruit.begin(1, 0); 
  bond_clear_prph(); // Clear all bonds, for testing purposes

  setup_device_and_device_information();
  setup_service();
  setup_peripheral();
  start_advertising();
  
  DEBUG_PRINT("Waiting for connections...");
}

void loop() {
  digitalToggle(LED_RED);

  if (Bluefruit.connected()) {
    uint16_t conn_handle = Bluefruit.connHandle();
    // If connected, send an indication with the current state
    // uint8_t state = digitalRead(LEAK_SENSOR_PIN); // Read sensor value
    uint8_t state = random(0, 2); // Simulate random state for testing
    leakseek_characteristic.indicate8(conn_handle, state); 
    DEBUG_PRINT("Wrote state: ");
    DEBUG_PRINT(state);
  } else {
    DEBUG_PRINT("Not connected");
  }

  delay(1000);
}

void setup_device_and_device_information() {
  // Set a clear GAP device name and TX power for better discoverability
  Bluefruit.setName("LeakSeek");
  Bluefruit.setTxPower(4); // 4 dBm is a reasonable, visible default

  // Configure and Start the Device Information Service
  bledis.setManufacturer("Kozak Industries");
  bledis.setModel("LeakSeek Drop 0.1");
  // We call begin() to register the DIS service with the BLE stack, given it's still a full BLE service with an associated UUID.
  bledis.begin();
}

void setup_service() {
  // Configure the service
  leakseek_service.begin();
  
  // Configure characteristic for reading and "notifying" (i.e. expect an ack)
  leakseek_characteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_INDICATE); // Consider adding CHR_PROPS_READ
  // Configure the characteristic to be readable but not writable
  leakseek_characteristic.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  leakseek_characteristic.setFixedLen(1); // Simple state, 1 byte, 0/1
  // leakseek_characteristic.setCccdWriteCallback(cccd_write_callback); // Optionally provide a "Client Characteristic Configuration" callback
  leakseek_characteristic.begin();
  leakseek_characteristic.write8(0);
}

void setup_peripheral() {
  // Configure peripheral
  Bluefruit.Periph.begin();
  // Set preferred connection interval
  Bluefruit.Periph.setConnIntervalMS(1000, 1100);
  // Feel free to ignore up to 5 pings from the central if nothing has changed.
  Bluefruit.Periph.setConnSlaveLatency(5);

  // Set the connection supervision timeout to 6 seconds; i.e. if we haven't received a pin after 6 seconds, consider the connection lost.
  Bluefruit.Periph.setConnSupervisionTimeout(6000);

  // Set the connect/disconnect callback handlers
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);
}

void start_advertising() {
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();

  // Include service and Service UUID
  Bluefruit.Advertising.addService(leakseek_service);

  // Include name
  Bluefruit.Advertising.addName();

  // There is no scan response packet in this example
  // Start Advertising
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds
}

void connect_callback(uint16_t conn_handle) {
  // Get current connection reference
  BLEConnection* connection = Bluefruit.Connection(conn_handle);

  // Get central name
  char central_name[32] = { 0 };
  connection->getPeerName(central_name, sizeof(central_name));
  DEBUG_PRINT(central_name);
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void) conn_handle;
  (void) reason;

  DEBUG_PRINT("Disconnected, reason = 0x");
  DEBUG_PRINT(reason, HEX);
  DEBUG_PRINT("Advertising...");
}

void wait_for_serial(int time) {
  unsigned long serial_wait_start = millis();
  while (!Serial && (millis() - serial_wait_start < time)) {
    delay(10);
  }
}