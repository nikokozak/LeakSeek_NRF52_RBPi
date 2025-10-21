// Define Manufacturer ID - there's an assigned-numbers list here:
// https://www.bluetooth.com/specifications/assigned-numbers/company-identifiers
// 0x0059 is Nordic
// 0x018B is Konica Minolta, Inc. (This is what we're using)
#define MANUFACTURER_ID 0x018B

// Pin for Pair/Reset button
#define BUTTON_PIN 2

// Pin for leak sensor (connected between GND and D9)
// Button is normally open, reads HIGH. When pressed, reads LOW.
#define LEAK_SENSOR_PIN 9

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

// Advertising intervals (in units of 0.625ms)
// Normal mode: 1600 = 1000ms, Alert mode: 32 = 20ms
#define ADV_INTERVAL_NORMAL 1600  // 1000ms between advertisements
#define ADV_INTERVAL_ALERT_FAST 32  // 20ms (fast burst for first 5 seconds)
#define ADV_INTERVAL_ALERT_SLOW 160  // 100ms (after initial burst)

// Loop delay (how often we check the sensor)
#define LOOP_DELAY_MS 1000  // Check sensor every 1 second

// Simulation mode settings (for demo without real sensor)
#define LEAK_CHANCE_PERCENT 5  // Percentage chance of leak per check (5 = 5%)
#define INCIDENT_COOLDOWN_MS 15000  // Minimum 15 seconds between incidents

// Debug Macro

#ifdef DEBUG
#define DEBUG_PRINT(...) if (Serial) { Serial.println(__VA_ARGS__); }
#else
#define DEBUG_PRINT(...) do {} while (0)
#endif 