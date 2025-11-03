// XIAO nRF52 Pin Diagnostic Tool
// Tests all pins to find which are ADC-capable and working

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);  // Wait for serial or 3s timeout

  Serial.println("\n========================================");
  Serial.println("XIAO nRF52 Pin Diagnostic Tool");
  Serial.println("========================================\n");

  Serial.println("Testing all pins for analog input capability...\n");

  // Test pins 0-10 (common XIAO range)
  for (int pin = 0; pin <= 10; pin++) {
    Serial.print("Pin ");
    Serial.print(pin);
    Serial.print(": ");

    // Configure as input (no pullup)
    pinMode(pin, INPUT);
    delay(10);

    // Read analog value
    int reading = analogRead(pin);

    Serial.print("ADC = ");
    Serial.print(reading);

    // Determine status
    if (reading == 0) {
      Serial.println(" - NOT WORKING or GROUNDED");
    } else if (reading < 100) {
      Serial.println(" - LOW (might be pulled down)");
    } else if (reading > 900) {
      Serial.println(" - HIGH (might be pulled up)");
    } else {
      Serial.println(" - FLOATING (normal for unconnected input)");
    }
  }

  Serial.println("\n========================================");
  Serial.println("Pin A0 & A1 Specific Test (CORRECT PINS)");
  Serial.println("========================================\n");

  // Test the CORRECT configuration using analog-capable pins
  pinMode(A0, INPUT);
  pinMode(A1, OUTPUT);

  Serial.println("Configuration: Pin A0 INPUT, Pin A1 OUTPUT LOW");
  digitalWrite(A1, LOW);
  delay(100);
  int reading_low = analogRead(A0);
  Serial.print("  Pin A0 reading: ");
  Serial.println(reading_low);

  Serial.println("\nConfiguration: Pin A0 INPUT, Pin A1 OUTPUT HIGH");
  digitalWrite(A1, HIGH);
  delay(100);
  int reading_high = analogRead(A0);
  Serial.print("  Pin A0 reading: ");
  Serial.println(reading_high);

  Serial.println("\n========================================");
  Serial.println("Analysis:");
  Serial.println("========================================");

  if (reading_low == 0 && reading_high == 0) {
    Serial.println("❌ PROBLEM: Pin A0 always reads 0");
    Serial.println("   Possible causes:");
    Serial.println("   1. Pin A0 is damaged");
    Serial.println("   2. Hardware short to ground");
    Serial.println("   3. ITO traces not connected to A0/A1");
  } else if (reading_low == reading_high) {
    Serial.println("⚠️  WARNING: Pin A0 doesn't change with pin A1");
    Serial.println("   ITO traces might not be connected");
    Serial.println("   Or PCB still wired to pins 7 & 8 instead of A0 & A1");
    Serial.println("\n   ACTION REQUIRED: Rewire PCB from pins 7/8 to A0/A1");
  } else {
    Serial.println("✅ WORKING: Pin A0 responds to pin A1 changes");
    Serial.print("   Range: ");
    Serial.print(reading_low);
    Serial.print(" to ");
    Serial.println(reading_high);
  }

  Serial.println("\n========================================");
  Serial.println("XIAO nRF52 Analog Pin Reference:");
  Serial.println("========================================");
  Serial.println("Pin Label → Arduino Pin Number → ADC Capable");
  Serial.println("A0  →  A0 or 2  →  YES");
  Serial.println("A1  →  A1 or 3  →  YES");
  Serial.println("A2  →  A2 or 4  →  YES");
  Serial.println("A3  →  A3 or 5  →  YES");
  Serial.println("A4  →  A4 or 28 →  YES");
  Serial.println("A5  →  A5 or 29 →  YES");
  Serial.println("D0-D10 → Varies →  SOME (check results above)");

  Serial.println("\n========================================");
  Serial.println("Recommended Pin Mapping (UPDATED):");
  Serial.println("========================================");
  Serial.println("ITO Sensor: A0 & A1 (MUST use analog pins!)");
  Serial.println("  - Old pins 7 & 8 are DIGITAL ONLY");
  Serial.println("  - PCB must be rewired to A0 & A1");
  Serial.println("Button: Keep on D0 & D1 (digital is fine)");
  Serial.println("Buzzer: Keep on D5 & D6 (digital is fine)");

  Serial.println("\n========================================");
  Serial.println("Diagnostic Complete");
  Serial.println("========================================\n");
}

void loop() {
  // Print continuous readings from pin A0 for live monitoring
  static unsigned long last_print = 0;

  if (millis() - last_print > 500) {
    int raw = analogRead(A0);
    Serial.print("Live Pin A0: ");
    Serial.print(raw);
    Serial.print("  Pin A1: ");
    Serial.println(digitalRead(A1) ? "HIGH" : "LOW");
    last_print = millis();
  }
}
