// Define Manufacturer ID - there's an assigned-numbers list here:
// https://www.bluetooth.com/specifications/assigned-numbers/company-identifiers
// 0x0059 is Nordic
// 0x018B is Konica Minolta, Inc. (This is what we're using)
#define MANUFACTURER_ID 0x018B

// Pin for Pair/Reset button
#define BUTTON_PIN 2

// #define UUID16_SVC_ALERT_NOTIFICATION                         0x1811, in BLEUuid.h
#define SERVICE_UUID UUID16_SVC_ALERT_NOTIFICATION

// #define UUID16_CHR_ALERT_STATUS                               0x2A3F
#define CHARACTERISTIC_UUID UUID16_CHR_ALERT_STATUS

// Custom ACK characteristic UUID for clearing alerts
// Using a custom 128-bit UUID: 6e400002-b5a3-f393-e0a9-e50e24dcca9e
#define ACK_CHARACTERISTIC_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"

// Manufacturer data protocol version
#define PROTOCOL_VERSION 1

// Advertising modes
#define ADV_MODE_NORMAL 0
#define ADV_MODE_ALERT 1

// Debug Macro

#ifdef DEBUG
#define DEBUG_PRINT(...) if (Serial) { Serial.println(__VA_ARGS__); }
#else
#define DEBUG_PRINT(...) do {} while (0)
#endif 