# LeakSeek Water Sensor Redesign Proposal

**Date**: November 30, 2025
**Status**: Draft - Rev 2 (Updated with peer review feedback)
**Purpose**: Document current issues and propose simplified sensor circuit

---

## 1. Background

LeakSeek uses ITO (Indium Tin Oxide) traces on a custom PCB to detect water presence. The sensor consists of two interlinked finger traces approximately 25cm in total length. When water bridges the gap between traces, the resistance drops significantly, indicating a leak.

### Current Hardware
- **MCU**: Seeed XIAO nRF52840
- **Sensor**: ITO two-trace interlinked-finger pattern
- **Trace resistance**: ~2kΩ (end-to-end, single trace)
- **Power**: CR2032 battery
- **Deployment environment**: Marine (sailboat bilge)

---

## 2. Current Implementation: RC Timing Method

### 2.1 Circuit Description

```
Pin 7 (OUTPUT) ────[ITO Traces (variable R)]──── Pin 8 (INPUT)
                                                      │
                                                   [100nF]
                                                      │
                                                     GND
```

### 2.2 Operating Principle

The firmware measures the RC time constant to infer resistance:
1. Discharge capacitor by driving Pin 7 LOW
2. Drive Pin 7 HIGH and start timer
3. Measure time until Pin 8 reaches logic HIGH threshold (~1.65V)
4. Longer time = higher resistance = dry; Shorter time = lower resistance = wet

**Expected values**:
| Condition | Resistance | Charge Time |
|-----------|------------|-------------|
| Dry | >1MΩ | 50-500ms |
| Wet | 10-100kΩ | 0.5-10ms |

### 2.3 Current Configuration

From `config.h`:
- Adaptive baseline learning (first 20 samples)
- 50% drop from baseline triggers detection
- 10-sample rolling average
- 500ms debounce period

---

## 3. Problems with Current Implementation

### 3.1 False Alarms

The system has experienced frequent false positive detections. Contributing factors:

#### 3.1.1 Timing Sensitivity
- Measurement depends on precise microsecond timing
- Any interrupt or BLE activity during measurement introduces error
- nRF52 SoftDevice (BLE stack) can preempt timing loops

#### 3.1.2 Baseline Drift
- "Dry" time constant varies with:
  - Temperature (resistivity changes ~2% per 10°C)
  - Humidity (surface conductivity)
  - Contamination (salt, oils from handling)
- Adaptive baseline algorithm can learn incorrect values

#### 3.1.3 Antenna Effects (25cm Trace Length)
- Long traces act as antenna, picking up:
  - 50/60Hz mains hum
  - RF interference (WiFi, BLE, nearby electronics)
  - Electrostatic coupling from movement
- Induced noise causes timing measurement jitter

#### 3.1.4 Threshold Selection Difficulty
- Measuring across 1000× range (500μs to 500ms)
- Geometric mean threshold calculation is sensitive to outliers
- Single threshold must accommodate all environmental conditions

### 3.2 Complexity

The current firmware is approximately 800 lines with:
- Adaptive baseline tracking
- Rolling average calculation
- Percentage-based threshold detection
- Multiple timeout and edge cases

This complexity increases the attack surface for bugs.

---

## 4. Proposed Solution: Analog Voltage Divider with Pulsed Excitation

### 4.1 Circuit Description

```
D7 (GPIO OUTPUT) ──[100kΩ R1]──┬── A0 (Analog Input)
                               │
                            [100nF C1]
                               │
                              GND
                               │
                        [ITO Trace A]
                               ↕ (detection gap)
                        [ITO Trace B]
                               │
                              GND
```

**Critical Design Note**: The pull-up resistor connects to a **GPIO pin (D7)**, NOT to 3.3V directly. This prevents galvanic corrosion (see Section 4.5).

### 4.2 Operating Principle

Simple resistive voltage divider with pulsed measurement:
1. Drive D7 HIGH (enable excitation)
2. Wait 50-100ms for capacitor settling
3. Read A0 (analog measurement)
4. Drive D7 LOW (disable excitation)
5. Sleep until next measurement cycle

**Voltage calculation**:
```
V_sense = 3.3V × (R_water / (R_water + R_pullup))
```

**Expected values**:
| Condition | R_water | V_sense | ADC Reading (10-bit) |
|-----------|---------|---------|----------------------|
| Dry | >1MΩ | ~3.3V | >1000 |
| Damp | 100kΩ | ~1.65V | ~512 |
| Wet | 50kΩ | ~1.1V | ~341 |
| Very wet | 10kΩ | ~0.3V | ~93 |

### 4.3 Low-Pass Filter

The 100nF capacitor forms an RC low-pass filter with the 100kΩ pull-up:

```
Cutoff frequency = 1 / (2π × R × C)
                 = 1 / (2π × 100kΩ × 100nF)
                 ≈ 16 Hz
```

**Filter characteristics**:
| Noise Source | Frequency | Attenuation |
|--------------|-----------|-------------|
| RF/WiFi/BLE | >1MHz | >99.99% |
| Mains hum | 50/60Hz | ~85% |
| Movement/static | 1-10Hz | Minimal (signal passes) |

**Settling time calculations**:
- Time constant: τ = R × C = 100kΩ × 100nF = 10ms
- 99% settling (5τ): 50ms
- **Firmware must wait ≥50ms after enabling D7 before reading ADC**

### 4.4 Component Selection Rationale

#### 100kΩ Pull-up Resistor
- High enough to limit current draw (<33μA from 3.3V)
- Low enough relative to dry impedance (>1MΩ) for clear dry reading
- Creates useful voltage division with expected wet resistance (10-100kΩ)
- Standard value; appropriate for tap water and salt water detection
- Note: 1MΩ would increase sensitivity to condensation but also increase noise susceptibility

#### 100nF Capacitor
- Aggressive filtering (16Hz cutoff) eliminates mains hum and RF
- Acceptable response time (~50ms settling) for leak detection
- Already available in current BOM
- Acts as charge reservoir for nRF52 SAADC internal sampling capacitor

### 4.5 Galvanic Corrosion Prevention (Critical)

**Problem with always-on excitation**:
If the pull-up resistor were connected directly to 3.3V:
- Current flows continuously through water when present
- Even micro-amps of DC current cause electrolysis
- ITO traces corrode rapidly; metal dendrites may form permanent shorts
- Battery drains unnecessarily during leak events

**Solution**: Pulsed excitation via GPIO
- Current only flows during measurement window (~100ms)
- At 1 measurement per second: 10% duty cycle
- At 1 measurement per 10 seconds: 1% duty cycle
- Corrosion rate reduced by 10-100×
- Also reduces power consumption

### 4.6 Hysteresis (Schmitt Trigger Logic)

To prevent oscillation when sensor is on threshold boundary, use dual thresholds:

```cpp
#define THRESHOLD_WET  800  // ADC counts - trigger alert below this
#define THRESHOLD_DRY  950  // ADC counts - clear alert above this

// State machine logic
if (state == DRY && reading < THRESHOLD_WET) {
    state = WET;
    triggerAlert();
} else if (state == WET && reading > THRESHOLD_DRY) {
    state = DRY;
    clearAlert();
}
// No state change if reading is between thresholds (dead zone)
```

**Dead zone**: 800-950 counts (~0.5V) prevents rapid toggling as water evaporates.

---

## 5. Comparison

| Aspect | RC Timing (Current) | Analog Divider (Proposed) |
|--------|---------------------|---------------------------|
| **Measurement type** | Time (μs) | Voltage (0-3.3V) |
| **Code complexity** | ~800 lines | ~50 lines |
| **Noise immunity** | Poor (timing-sensitive) | Good (hardware filtered) |
| **Baseline tracking** | Required (adaptive) | Not required (fixed threshold) |
| **BLE interference** | Problematic | None |
| **Component count** | 1 capacitor | 1 resistor + 1 capacitor |
| **Dynamic range** | 1000× | 10× (adequate) |
| **Response time** | Variable (0.5-500ms measurement) | Fixed (~50ms settling) |
| **Corrosion risk** | Present (always-on in current impl) | Minimal (pulsed excitation) |
| **Failure modes** | Many | Few |

---

## 6. Implementation Details

### 6.1 Hardware Changes

**Remove**:
- Connection from ITO Trace A to Pin 7
- Connection from ITO Trace B to Pin 8
- (Keep 100nF capacitor, reposition)

**Add**:
- 100kΩ resistor between D7 and A0
- 100nF capacitor between A0 and GND
- ITO Trace A to A0
- ITO Trace B to GND

### 6.2 Pin Assignments (Proposed)

| Pin | Function | Notes |
|-----|----------|-------|
| A0 | Water sense | Analog input with filter cap |
| D7 | Sensor power | GPIO output, drives pull-up resistor |
| GND | ITO Trace B | Direct ground reference |
| D0 | Button A | INPUT_PULLUP (unchanged) |
| D1 | Button B | OUTPUT LOW (unchanged) |
| D4 | Buzzer - | (unchanged) |
| D6 | Buzzer + | (unchanged) |

Pin D8 becomes available for other uses.

### 6.3 Firmware Changes

**Replace**:
- RC timing measurement loop
- Adaptive baseline tracking
- Rolling average calculation
- Percentage-based detection

**With**:
```cpp
#define SENSOR_POWER_PIN 7
#define SENSOR_READ_PIN A0
#define SETTLING_TIME_MS 50
#define THRESHOLD_WET 800
#define THRESHOLD_DRY 950

bool sensor_state = false;  // false = dry, true = wet

void setup() {
    pinMode(SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_PIN, LOW);
    analogReadResolution(10);  // 10-bit ADC (0-1023)
    // Configure SAADC acquisition time if needed for high source impedance
}

bool readWaterSensor() {
    // Enable sensor excitation
    digitalWrite(SENSOR_POWER_PIN, HIGH);

    // Wait for RC filter to settle (5τ = 50ms for 99%)
    delay(SETTLING_TIME_MS);

    // Read ADC
    int reading = analogRead(SENSOR_READ_PIN);

    // Disable sensor excitation (prevent corrosion)
    digitalWrite(SENSOR_POWER_PIN, LOW);

    // Hysteresis logic
    if (!sensor_state && reading < THRESHOLD_WET) {
        sensor_state = true;  // Transition to WET
    } else if (sensor_state && reading > THRESHOLD_DRY) {
        sensor_state = false; // Transition to DRY
    }
    // If between thresholds, maintain current state

    return sensor_state;
}
```

### 6.4 nRF52 SAADC Configuration Note

The nRF52's SAADC has finite input impedance due to internal sampling capacitor charging. With 100kΩ source impedance:
- Ensure acquisition time (TACQ) is set to maximum (40μs) if using Nordic SDK directly
- The external 100nF capacitor acts as a charge reservoir, mitigating this issue
- Arduino `analogRead()` typically uses adequate acquisition time by default

---

## 7. Risk Assessment

### 7.1 Risks of Proposed Change

| Risk | Likelihood | Mitigation |
|------|------------|------------|
| Threshold too sensitive | Medium | Dual-threshold hysteresis; bench calibration |
| Threshold too conservative | Medium | Testing with various water conductivities |
| New hardware introduces issues | Low | Simple circuit; bench test before deployment |
| Reduced detection of low-conductivity water | Low | 100kΩ pull-up gives good sensitivity |
| Flux residue causes false readings | Medium | Specify thorough PCB cleaning in fab notes |

### 7.2 Risks of Keeping Current System

| Risk | Likelihood | Impact |
|------|------------|--------|
| Continued false alarms | High | User fatigue; ignored real alerts |
| Missed detection due to baseline drift | Medium | Undetected leak |
| Difficult debugging | High | Extended development time |
| Galvanic corrosion (if always-on) | Medium | Sensor degradation over time |

---

## 8. Testing Plan

### 8.1 Bench Testing

1. **Dry reading stability**: Monitor ADC over 24 hours, verify stable >950
2. **Wet detection**: Test with tap water, distilled water, salt water
3. **Noise immunity**: Operate near WiFi router, fluorescent lights, phone
4. **Temperature stability**: Test at 10°C, 25°C, 40°C
5. **Hysteresis verification**: Confirm no oscillation at threshold boundary

### 8.2 Threshold Determination

1. Record dry readings (expect 980-1023)
2. Record wet readings with tap water (expect 100-500)
3. Record wet readings with salt water (expect very low, verify no issues)
4. Record wet readings with distilled water (worst case - expect 400-700)
5. Set THRESHOLD_WET conservatively (e.g., 800)
6. Set THRESHOLD_DRY with adequate margin (e.g., 950)

### 8.3 Integration Testing

1. Full system test with BLE advertising active
2. Alert flow verification (detection → buzzer → ACK → clear)
3. Battery life measurement (should improve with pulsed excitation)
4. Long-term soak test: 1 week continuous operation

### 8.4 Environmental Testing

1. **Salt spray exposure**: Simulate marine environment
2. **Humidity cycling**: Verify no false triggers from condensation
3. **Vibration**: Ensure connections remain stable

---

## 9. Resolved Review Questions

Based on peer review feedback, the following questions have been addressed:

| Question | Resolution |
|----------|------------|
| Is 100kΩ pull-up appropriate? | **Yes.** Standard value for water detection. 1MΩ too noise-sensitive; 10kΩ too power-hungry. |
| Should we implement hysteresis? | **Yes, mandatory.** Dual thresholds (800/950) prevent oscillation. See Section 4.6. |
| Other noise sources to consider? | **Flux residue** on PCB can be conductive. Specify thorough cleaning. **Salt creep** in marine environment may require periodic recalibration. |
| Retain adaptive baseline? | **No.** Fixed thresholds with generous margin. If drift is enough to false-trigger, sensor needs cleaning anyway. |
| Water conductivity test range? | **Tap, salt, and distilled water.** Salt water is extremely conductive (verify no brown-out). Distilled is worst case for sensitivity. |

---

## 10. Conclusion

The proposed analog voltage divider with pulsed excitation and RC filtering addresses the root causes of false alarms in the current system:

- **Eliminates timing sensitivity** by using voltage measurement
- **Hardware-level noise filtering** instead of software averaging
- **Dramatically simpler firmware** reduces bug surface area
- **Pulsed excitation prevents galvanic corrosion** of ITO traces
- **Hysteresis prevents oscillation** at threshold boundaries
- **Uses existing components** (capacitor already in BOM)

The tradeoff is a fixed ~50ms settling time per measurement, which is acceptable for leak detection where 1-second latency is sufficient.

---

## Appendix A: Bill of Materials Change

| Component | Current | Proposed | Delta |
|-----------|---------|----------|-------|
| 100nF capacitor | 1 | 1 | 0 |
| 100kΩ resistor | 0 | 1 | +1 |

**Net change**: +1 resistor

---

## Appendix B: Schematic

```
                    ┌─────────────────────────────────────┐
                    │           XIAO nRF52840             │
                    │                                     │
        ┌───────────┤ D7 (GPIO OUT)                       │
        │           │                                     │
      [100kΩ]       │                                     │
        │           │                                     │
        ├───────────┤ A0 (ADC IN)                         │
        │           │                                     │
        │           │                              GND ───┼───┐
        │           └─────────────────────────────────────┘   │
        │                                                     │
     [100nF]                                                  │
        │                                                     │
        ├─────────────────────────────────────────────────────┤
        │                                                     │
  [ITO Trace A]                                               │
        ↕ ← water detection gap                               │
  [ITO Trace B]                                               │
        │                                                     │
        └─────────────────────────────────────────────────────┘
                              GND
```

---

## Appendix C: Firmware Pseudocode

```cpp
// Configuration
#define SENSOR_POWER_PIN    7       // GPIO to control pull-up
#define SENSOR_READ_PIN     A0      // Analog input
#define SETTLING_TIME_MS    50      // 5τ for 99% settling
#define THRESHOLD_WET       800     // Below this = wet
#define THRESHOLD_DRY       950     // Above this = dry
#define DEBOUNCE_COUNT      3       // Consecutive readings to confirm

// State
bool water_detected = false;
int debounce_counter = 0;

void setup() {
    pinMode(SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_PIN, LOW);
    analogReadResolution(10);
}

void loop() {
    // Power on sensor
    digitalWrite(SENSOR_POWER_PIN, HIGH);
    delay(SETTLING_TIME_MS);

    // Read
    int reading = analogRead(SENSOR_READ_PIN);

    // Power off sensor (prevent corrosion)
    digitalWrite(SENSOR_POWER_PIN, LOW);

    // Hysteresis state machine with debounce
    if (!water_detected) {
        if (reading < THRESHOLD_WET) {
            debounce_counter++;
            if (debounce_counter >= DEBOUNCE_COUNT) {
                water_detected = true;
                triggerAlert();
                debounce_counter = 0;
            }
        } else {
            debounce_counter = 0;
        }
    } else {
        if (reading > THRESHOLD_DRY) {
            debounce_counter++;
            if (debounce_counter >= DEBOUNCE_COUNT) {
                water_detected = false;
                clearAlert();
                debounce_counter = 0;
            }
        } else {
            debounce_counter = 0;
        }
    }

    // Sleep until next check
    delay(1000);  // Or use deep sleep for power savings
}
```

---

## Appendix D: References

- nRF52840 SAADC specifications: 12-bit SAR, 0-3.6V range, configurable acquisition time
- ITO trace characteristics: ~2kΩ per trace (measured)
- Current firmware: `leakseek_firmware/leakseek_firmware.ino` (v2.3.0-rc-timing)
- Peer review feedback: Gemini (November 30, 2025)

---

## Revision History

| Rev | Date | Author | Changes |
|-----|------|--------|---------|
| 1 | 2025-11-30 | Initial | Initial draft |
| 2 | 2025-11-30 | Updated | Added galvanic corrosion prevention (pulsed excitation via GPIO), hysteresis logic, SAADC configuration notes, resolved review questions, added schematics and pseudocode |
