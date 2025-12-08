// LeakSeek Sensor Test Sketch - Analog Voltage Divider
// For testing and characterizing the new ITO water sensor circuit
// Hardware: XIAO nRF52840, 100kΩ pull-up on D5, 100nF cap on A0-A1, ITO traces
//
// Wiring:
//   D5 ──[100kΩ]──┬── A0 (ADC)
//                 │
//              [ITO A]
//                 ↕
//              [ITO B]
//                 │
//   A1 ──[100nF]──┴── GND

#define SENSOR_POWER_PIN 5      // D5 - GPIO to drive pull-up
#define SENSOR_READ_PIN A0      // A0 - ADC input
#define SETTLING_TIME_MS 50     // Wait for RC filter to settle (5τ)

// Session statistics (reset with 'r')
unsigned long sample_count = 0;
unsigned long sum = 0;
unsigned long sum_sq = 0;  // For variance calculation
int session_min = 1023;
int session_max = 0;
int last_reading = 0;

// All-time statistics (persist until power cycle)
int alltime_min = 1023;
int alltime_max = 0;
unsigned long alltime_min_time = 0;  // When min occurred (ms since boot)
unsigned long alltime_max_time = 0;  // When max occurred (ms since boot)

// Jump/drift detection
int previous_reading = -1;
#define JUMP_THRESHOLD 50  // Report jumps larger than this
int largest_jump = 0;
int largest_jump_from = 0;
int largest_jump_to = 0;
unsigned long largest_jump_time = 0;

// Timing
unsigned long start_time = 0;
unsigned long last_print_time = 0;
#define PRINT_INTERVAL_MS 1000  // Print stats every second

void setup() {
  Serial.begin(115200);

  // Wait for serial connection (with timeout for standalone operation)
  unsigned long serial_wait_start = millis();
  while (!Serial && (millis() - serial_wait_start < 3000)) {
    delay(10);
  }

  // Configure pins
  pinMode(SENSOR_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, LOW);  // Start with sensor off

  // Configure ADC
  analogReadResolution(10);  // 10-bit (0-1023)

  Serial.println();
  Serial.println("========================================");
  Serial.println("LeakSeek Sensor Test - Analog Divider");
  Serial.println("========================================");
  Serial.println();
  Serial.println("Wiring check:");
  Serial.println("  D5 -> 100k resistor -> A0");
  Serial.println("  A0 -> ITO Trace A");
  Serial.println("  A0 -> 100nF cap -> A1");
  Serial.println("  A1 -> GND (hardwired)");
  Serial.println("  ITO Trace B -> A1/GND");
  Serial.println();
  Serial.println("Expected readings:");
  Serial.println("  Dry:    950-1023 (~3.0-3.3V)");
  Serial.println("  Damp:   500-800  (~1.6-2.6V)");
  Serial.println("  Wet:    100-500  (~0.3-1.6V)");
  Serial.println("  Shorted: 0-100   (~0-0.3V)");
  Serial.println();
  Serial.println("Commands (send via Serial):");
  Serial.println("  r - Reset session statistics (all-time preserved)");
  Serial.println("  s - Print summary with suggested thresholds");
  Serial.println("  g - Toggle graph mode (for Serial Plotter)");
  Serial.println();
  Serial.println("Starting measurements...");
  Serial.println();

  start_time = millis();
  last_print_time = millis();
}

bool graph_mode = false;

int readSensor() {
  // Power on
  digitalWrite(SENSOR_POWER_PIN, HIGH);

  // Wait for settling
  delay(SETTLING_TIME_MS);

  // Read ADC
  int reading = analogRead(SENSOR_READ_PIN);

  // Power off (prevent corrosion)
  digitalWrite(SENSOR_POWER_PIN, LOW);

  return reading;
}

void resetStats() {
  sample_count = 0;
  sum = 0;
  sum_sq = 0;
  session_min = 1023;
  session_max = 0;
  start_time = millis();
  Serial.println("\n>>> Session statistics reset (all-time preserved) <<<\n");
}

void printSummary() {
  Serial.println();
  Serial.println("================== SUMMARY ==================");
  Serial.print("All-time MIN: ");
  Serial.print(alltime_min);
  Serial.print(" at ");
  Serial.print(alltime_min_time / 1000.0, 1);
  Serial.println("s");

  Serial.print("All-time MAX: ");
  Serial.print(alltime_max);
  Serial.print(" at ");
  Serial.print(alltime_max_time / 1000.0, 1);
  Serial.println("s");

  Serial.print("All-time RANGE: ");
  Serial.println(alltime_max - alltime_min);

  Serial.print("Largest JUMP: ");
  Serial.print(largest_jump);
  Serial.print(" (");
  Serial.print(largest_jump_from);
  Serial.print(" -> ");
  Serial.print(largest_jump_to);
  Serial.print(") at ");
  Serial.print(largest_jump_time / 1000.0, 1);
  Serial.println("s");

  Serial.println();
  Serial.println("Suggested thresholds based on data:");
  Serial.print("  THRESHOLD_DRY: ");
  Serial.println(alltime_max - 50);
  Serial.print("  THRESHOLD_WET: ");
  Serial.println(alltime_min + 100);
  Serial.println("=============================================");
  Serial.println();
}

void loop() {
  // Check for serial commands
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'r' || cmd == 'R') {
      resetStats();
    } else if (cmd == 'g' || cmd == 'G') {
      graph_mode = !graph_mode;
      if (graph_mode) {
        Serial.println("\n>>> Graph mode ON (use Serial Plotter) <<<\n");
      } else {
        Serial.println("\n>>> Graph mode OFF <<<\n");
      }
    } else if (cmd == 's' || cmd == 'S') {
      printSummary();
    }
  }

  // Take a reading
  int reading = readSensor();
  last_reading = reading;

  // Update session statistics
  sample_count++;
  sum += reading;
  sum_sq += (unsigned long)reading * reading;
  if (reading < session_min) session_min = reading;
  if (reading > session_max) session_max = reading;

  // Update all-time statistics
  if (reading < alltime_min) {
    alltime_min = reading;
    alltime_min_time = millis();
    Serial.print(">>> NEW ALL-TIME MIN: ");
    Serial.print(reading);
    Serial.println(" <<<");
  }
  if (reading > alltime_max) {
    alltime_max = reading;
    alltime_max_time = millis();
    Serial.print(">>> NEW ALL-TIME MAX: ");
    Serial.print(reading);
    Serial.println(" <<<");
  }

  // Jump detection
  if (previous_reading >= 0) {
    int jump = abs(reading - previous_reading);
    if (jump >= JUMP_THRESHOLD) {
      Serial.print(">>> JUMP DETECTED: ");
      Serial.print(previous_reading);
      Serial.print(" -> ");
      Serial.print(reading);
      Serial.print(" (delta: ");
      Serial.print(reading - previous_reading);
      Serial.println(") <<<");
    }
    if (jump > largest_jump) {
      largest_jump = jump;
      largest_jump_from = previous_reading;
      largest_jump_to = reading;
      largest_jump_time = millis();
    }
  }
  previous_reading = reading;

  // Output
  if (graph_mode) {
    // Simple output for Serial Plotter
    // Format: Reading, AllTimeMin, AllTimeMax, ThresholdWet, ThresholdDry
    Serial.print(reading);
    Serial.print(",");
    Serial.print(alltime_min);
    Serial.print(",");
    Serial.print(alltime_max);
    Serial.print(",");
    Serial.print(800);   // THRESHOLD_WET reference line
    Serial.print(",");
    Serial.println(950); // THRESHOLD_DRY reference line
  } else {
    // Print stats every second
    if (millis() - last_print_time >= PRINT_INTERVAL_MS) {
      last_print_time = millis();

      float avg = (float)sum / sample_count;
      float variance = ((float)sum_sq / sample_count) - (avg * avg);
      float stddev = sqrt(variance);
      float voltage = (reading / 1023.0) * 3.3;
      float elapsed_sec = (millis() - start_time) / 1000.0;

      Serial.print("ADC: ");
      Serial.print(reading);
      Serial.print(" (");
      Serial.print(voltage, 2);
      Serial.print("V) | Avg: ");
      Serial.print(avg, 1);
      Serial.print(" | StdDev: ");
      Serial.print(stddev, 2);
      Serial.print(" | Session[");
      Serial.print(session_min);
      Serial.print("-");
      Serial.print(session_max);
      Serial.print("] | AllTime[");
      Serial.print(alltime_min);
      Serial.print("-");
      Serial.print(alltime_max);
      Serial.print("] | n=");
      Serial.print(sample_count);
      Serial.print(" (");
      Serial.print(elapsed_sec, 1);
      Serial.print("s)");

      // Status interpretation
      Serial.print(" | ");
      if (reading > 950) {
        Serial.println("DRY");
      } else if (reading > 800) {
        Serial.println("DRY (near threshold)");
      } else if (reading > 500) {
        Serial.println("DAMP");
      } else if (reading > 100) {
        Serial.println("WET");
      } else {
        Serial.println("SHORTED/VERY WET");
      }
    }
  }

  // Small delay between readings (adjust as needed)
  delay(100);  // ~10 readings per second
}
