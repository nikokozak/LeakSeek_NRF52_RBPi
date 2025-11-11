// Define Manufacturer ID - there's an assigned-numbers list here:
// https://www.bluetooth.com/specifications/assigned-numbers/company-identifiers
// 0x0059 is Nordic
// 0x018B is Konica Minolta, Inc. (This is what we're using)
#define MANUFACTURER_ID 0x018B

// ============================================
// XIAO nRF52 Custom PCB Pin Assignments
// ============================================

// Water sensor: ITO two-trace interlinked-finger sensor
// RC timing firmware uses digital pins 7 & 8

// Pin assignments for RC timing firmware (current hardware)
#define WATER_SENSE_PIN_A 7    // Digital output for RC timing
#define WATER_SENSE_PIN_B 8    // Digital input with 100nF cap to GND

// Button for acknowledging alert (1-second hold to stop)
#define BUTTON_PIN_A 0
#define BUTTON_PIN_B 1

// Buzzer (3.3V) - direct drive
#define BUZZER_PIN_POSITIVE 6
#define BUZZER_PIN_NEGATIVE 4

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
// Water Detection Configuration - ADC Method
// ============================================
// For leakseek_firmware_water_sensor.ino (requires analog pins A0/A1)
// Water detection uses resistance measurement between ITO traces via ADC
// Dry: High impedance (>1MΩ) → High ADC reading (700-1023)
// Wet: Low impedance (<100kΩ) → Low ADC reading (0-500)

#define WATER_DETECTION_ENABLED true    // Set false to use simulation mode
#define WATER_THRESHOLD_DEFAULT 800      // ADC threshold (0-1023)
#define WATER_SAMPLE_COUNT 30            // Number of samples to average for stability
#define WATER_DETECTION_DEBOUNCE_MS 500  // Debounce time before confirming water (increased from 200ms)

// Water detection logic mode
// false = Normal (water DECREASES reading, trigger when reading < threshold)
// true  = Inverted (water INCREASES reading, trigger when reading > threshold)
#define WATER_DETECTION_INVERTED false   // Set true if wet readings are HIGHER than dry

// ============================================
// Water Detection Configuration - RC Timing Method
// ============================================
// For leakseek_firmware_rc_timing.ino (works with digital pins 7/8!)
// Uses RC time constant measurement with 100nF capacitor
// Dry: High resistance (>1MΩ) → Slow charge → Long time (50-500ms)
// Wet: Low resistance (<100kΩ) → Fast charge → Short time (0.5-5ms)

// ADAPTIVE THRESHOLD APPROACH - uses percentage-based detection
// Instead of fixed threshold, we detect water as a dramatic DROP from baseline
#define RC_WATER_DROP_PERCENTAGE 80      // Water must reduce RC time by 80% from baseline
                                         // Example: If baseline is 50ms, water is < 10ms (50% of 50ms = 10ms)
                                         // This adapts to environmental drift automatically!

#define RC_ABSOLUTE_MIN_THRESHOLD_US 10000   // Absolute minimum "wet" threshold (10ms)
                                             // Even if percentage logic fails, anything <10ms is definitely wet
                                             // Actual water is typically 0.5-5ms, so 10ms is very safe

#define RC_ABSOLUTE_MAX_DRY_US 150000    // Absolute maximum expected "dry" time (150ms)
                                         // Used to validate baseline and reject bad readings
                                         // With 330k + high-resistance traces, can be up to 133ms

#define RC_SAMPLE_COUNT 10               // Increased from 5 to 10 for better noise rejection
                                         // 10 samples at 100ms = 1 second of averaging
                                         // Higher = more stable, slower response
                                         // Recommended: 10-20

#define RC_BASELINE_SAMPLES 20           // Number of initial samples to establish baseline
                                         // First 20 readings (2 seconds) are used to learn "dry" state

#define RC_DETECTION_INVERTED false      // Set true if water INCREASES charge time
                                         // (unusual, but possible with some circuits)

// ============================================
// Button Configuration
// ============================================
#define BUTTON_HOLD_TIME_MS 1000         // 1 second hold to acknowledge alert
#define BUTTON_DEBOUNCE_MS 50            // Debounce time for button press

// ============================================
// Buzzer Configuration
// ============================================
// Changed to single loud beep pattern for better audibility
#define BUZZER_BEEP_DURATION_MS 200      // Each beep lasts 200ms (longer = louder)
#define BUZZER_BEEP_PAUSE_MS 1800        // Pause between beeps (200ms beep + 1800ms pause = 2 second cycle)
#define BUZZER_BEEPS_PER_SEQUENCE 1      // 1 beep per sequence (loud and simple)
#define BUZZER_SEQUENCE_PAUSE_MS 0       // No extra pause needed with single beep pattern

// Square wave generation for piezo buzzers
#define BUZZER_FREQUENCY_HZ 4000         // 4kHz - resonant frequency for this piezo buzzer
#define BUZZER_TOGGLE_INTERVAL_US (1000000 / (BUZZER_FREQUENCY_HZ * 2))  // Half-period in microseconds

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
#define DEBUG_MODE 1

// Graph debug refresh rate (only used if DEBUG_MODE == 2)
#define GRAPH_DEBUG_INTERVAL_MS 100  // Update graph every 100ms

// Debug Macros
#if DEBUG_MODE == 1
  #define DEBUG_PRINT(...) if (Serial) { Serial.println(__VA_ARGS__); }
#else
  #define DEBUG_PRINT(...) do {} while (0)
#endif 