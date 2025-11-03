// Define Manufacturer ID - there's an assigned-numbers list here:
// https://www.bluetooth.com/specifications/assigned-numbers/company-identifiers
// 0x0059 is Nordic
// 0x018B is Konica Minolta, Inc. (This is what we're using)
#define MANUFACTURER_ID 0x018B

// ============================================
// XIAO nRF52 Custom PCB Pin Assignments
// ============================================

// Water sensor: ITO two-trace interlinked-finger sensor
// Pin 8 has 100nF capacitor to GND for filtering
#define WATER_SENSE_PIN_A 7   // Analog input for water detection
#define WATER_SENSE_PIN_B 8   // Reference/ground side with cap

// Button for acknowledging alert (1-second hold to stop)
#define BUTTON_PIN_A 0
#define BUTTON_PIN_B 1

// Buzzer (3.3V) - direct drive
#define BUZZER_PIN_POSITIVE 5
#define BUZZER_PIN_NEGATIVE 6

// Legacy definitions (kept for compatibility)
#define BUTTON_PIN 2          // Not used on custom PCB
#define LEAK_SENSOR_PIN 9     // Not used on custom PCB

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
#define LOOP_DELAY_MS 100  // Check sensor every 100ms for quick response

// ============================================
// Water Detection Configuration
// ============================================
// Water detection uses resistance measurement between ITO traces
// Dry: High impedance (>1MΩ), Wet: Low impedance (<100kΩ)
// The 100nF cap on pin 8 provides filtering

#define WATER_DETECTION_ENABLED true    // Set false to use simulation mode
#define WATER_THRESHOLD_DEFAULT 800      // ADC threshold (0-1023). Lower = more sensitive
#define WATER_SAMPLE_COUNT 30             // Number of samples to average for stability
#define WATER_DETECTION_DEBOUNCE_MS 200  // Debounce time before confirming water

// ============================================
// Button Configuration
// ============================================
#define BUTTON_HOLD_TIME_MS 1000         // 1 second hold to acknowledge alert
#define BUTTON_DEBOUNCE_MS 50            // Debounce time for button press

// ============================================
// Buzzer Configuration
// ============================================
#define BUZZER_BEEP_DURATION_MS 100      // Each beep lasts 100ms
#define BUZZER_BEEP_PAUSE_MS 100         // Pause between beeps in a sequence
#define BUZZER_BEEPS_PER_SEQUENCE 3      // 3 beeps per sequence
#define BUZZER_SEQUENCE_PAUSE_MS 1000    // Pause between sequences

// ============================================
// System States
// ============================================
#define STATE_NORMAL 0    // Normal monitoring mode
#define STATE_ALERT 1     // Alert mode (leak detected, buzzing)
#define STATE_STOPPED 2   // Stopped (acknowledged, waiting for reboot)

// Simulation mode settings (for demo without real sensor - legacy)
#define LEAK_CHANCE_PERCENT 5  // Percentage chance of leak per check (5 = 5%)
#define INCIDENT_COOLDOWN_MS 15000  // Minimum 15 seconds between incidents

// ============================================
// Debug Configuration
// ============================================
// Set DEBUG_MODE to choose debug output style:
// 0 = No debug output
// 1 = Text debug (detailed messages)
// 2 = Graph debug (numeric values for Serial Plotter)
#define DEBUG_MODE 2

// Graph debug refresh rate (only used if DEBUG_MODE == 2)
#define GRAPH_DEBUG_INTERVAL_MS 100  // Update graph every 100ms

// Debug Macros
#if DEBUG_MODE == 1
  #define DEBUG_PRINT(...) if (Serial) { Serial.println(__VA_ARGS__); }
#else
  #define DEBUG_PRINT(...) do {} while (0)
#endif 