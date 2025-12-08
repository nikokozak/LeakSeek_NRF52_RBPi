// LeakSeek BLE Advertising
// ============================================

#ifndef BLE_H
#define BLE_H

#include <bluefruit.h>
#include "config.h"

// ============================================
// BLE State
// ============================================

extern uint8_t ble_adv_mode;
extern unsigned long ble_alert_start_time;

// ============================================
// BLE Objects (must be defined in main .ino)
// ============================================

extern BLEService leakseek_service;
extern BLECharacteristic leakseek_characteristic;
extern BLECharacteristic ack_characteristic;
extern BLEDis bledis;

// ============================================
// Callback Type for ACK
// ============================================

typedef void (*AckCallback)();

// ============================================
// Function Declarations
// ============================================

// Initialize BLE stack and services
void ble_init();

// Set advertising mode (ADV_MODE_NORMAL or ADV_MODE_ALERT)
// flags: current status flags (bit0=leak, bit1=needs_ack)
// seq: current sequence number
// battery: battery percentage
void ble_set_advertising(uint8_t mode, uint8_t flags, uint8_t seq, uint8_t battery);

// Update advertising data without changing mode
// Call this when flags/seq/battery change but mode stays the same
void ble_update_advertising(uint8_t flags, uint8_t seq, uint8_t battery);

// Check if we should transition from fast to slow alert advertising
// Returns true if transition occurred
bool ble_check_alert_timing();

// ============================================
// Implementation
// ============================================

uint8_t ble_adv_mode = ADV_MODE_NORMAL;
unsigned long ble_alert_start_time = 0;

// Store current advertising data for updates
static uint8_t _current_flags = 0;
static uint8_t _current_seq = 0;
static uint8_t _current_battery = 100;

void ble_init() {
  Bluefruit.begin();
  Bluefruit.setTxPower(0);
  Bluefruit.setName("LeakSeek");

  // Connection parameters
  Bluefruit.Periph.setConnIntervalMS(15, 30);
  Bluefruit.Periph.setConnSlaveLatency(5);
  Bluefruit.Periph.setConnSupervisionTimeout(4000);

  // Device Information Service
  bledis.setManufacturer("LeakSeek");
  bledis.setModel("nRF52-Analog-v3");
  bledis.begin();

  // LeakSeek Service
  leakseek_service.begin();

  // Status Characteristic (read/notify)
  leakseek_characteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  leakseek_characteristic.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  leakseek_characteristic.setFixedLen(1);
  leakseek_characteristic.begin();
  leakseek_characteristic.write8(0);

  // ACK Characteristic (write)
  ack_characteristic.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  ack_characteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  ack_characteristic.setFixedLen(1);
  ack_characteristic.begin();
  ack_characteristic.write8(0);

  DEBUG_PRINT("BLE initialized");
}

void ble_set_advertising(uint8_t mode, uint8_t flags, uint8_t seq, uint8_t battery) {
  ble_adv_mode = mode;
  _current_flags = flags;
  _current_seq = seq;
  _current_battery = battery;

  if (mode == ADV_MODE_ALERT) {
    ble_alert_start_time = millis();
  }

  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();

  // Standard advertising data
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();

  // Manufacturer data: 6 bytes
  uint8_t adv_data[6];
  adv_data[0] = MANUFACTURER_ID & 0xFF;
  adv_data[1] = (MANUFACTURER_ID >> 8) & 0xFF;
  adv_data[2] = PROTOCOL_VERSION;
  adv_data[3] = flags;
  adv_data[4] = seq;
  adv_data[5] = battery;
  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, adv_data, 6);

  if (mode == ADV_MODE_ALERT) {
    // Alert mode: connectable, fast then slow intervals
    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED);

    unsigned long elapsed = millis() - ble_alert_start_time;
    if (elapsed < 5000) {  // First 5 seconds: fast advertising
      Bluefruit.Advertising.setInterval(ADV_INTERVAL_ALERT_FAST, ADV_INTERVAL_ALERT_FAST + 16);
    } else {
      Bluefruit.Advertising.setInterval(ADV_INTERVAL_ALERT_SLOW, ADV_INTERVAL_ALERT_SLOW + 16);
    }
    Bluefruit.Advertising.setFastTimeout(30);
  } else {
    // Normal mode: non-connectable, slow intervals
    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);
    Bluefruit.Advertising.setInterval(ADV_INTERVAL_NORMAL, ADV_INTERVAL_NORMAL);
  }

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0);

  DEBUG_PRINTF("Advertising: %s (flags=0x%02X, seq=%d)\n",
               mode == ADV_MODE_ALERT ? "ALERT" : "NORMAL", flags, seq);
}

void ble_update_advertising(uint8_t flags, uint8_t seq, uint8_t battery) {
  // Only rebuild if data actually changed
  if (flags == _current_flags && seq == _current_seq && battery == _current_battery) {
    return;
  }
  ble_set_advertising(ble_adv_mode, flags, seq, battery);
}

bool ble_check_alert_timing() {
  if (ble_adv_mode != ADV_MODE_ALERT) {
    return false;
  }

  // Check if we need to transition from fast to slow advertising
  unsigned long elapsed = millis() - ble_alert_start_time;
  if (elapsed >= 5000) {
    // Re-set advertising to update interval
    ble_set_advertising(ADV_MODE_ALERT, _current_flags, _current_seq, _current_battery);
    return true;
  }
  return false;
}

#endif // BLE_H
