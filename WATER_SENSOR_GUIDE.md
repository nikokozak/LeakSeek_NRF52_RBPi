# LeakSeek Water Sensor Implementation Guide

## Hardware Setup - XIAO nRF52 Custom PCB

### Components
- **Power**: CR2032 coin cell → 3.3V/GND
- **Water Sensor**: ITO two-trace interlinked-finger sensor (Pins 7 & 8)
  - Pin 8 has 100nF capacitor to GND for filtering
- **Button**: Standard pushbutton (Pins 0 & 1)
- **Buzzer**: 3.3V buzzer (Pins 5 & 6)

### Pin Assignments
```
Pin 7: Water Sense A (Analog input)
Pin 8: Water Sense B (Reference, 100nF cap to GND)
Pin 0: Button A (INPUT_PULLUP)
Pin 1: Button B (OUTPUT LOW - GND)
Pin 5: Buzzer Positive
Pin 6: Buzzer Negative
```

---

## Firmware: leakseek_firmware_water_sensor.ino

### Key Features

#### 1. Water Detection
- **Method**: Resistance/capacitance measurement between ITO traces
- **Principle**:
  - Dry: High impedance (>1MΩ) → High ADC reading (800-1023)
  - Wet: Low impedance (<100kΩ) → Low ADC reading (0-500)

- **Sampling**: Rolling average of 5 readings for stability
- **Debouncing**: Water must be detected continuously for 200ms before triggering alert

#### 2. State Machine
```
NORMAL → (water detected) → ALERT → (button held 1s) → STOPPED
                                ↑
                                └─── (BLE ACK received) ──┘
```

- **NORMAL**: Monitoring for water, LED blinks slowly (2s)
- **ALERT**: Water detected, buzzer active, LED blinks rapidly (100ms)
- **STOPPED**: Alert acknowledged, buzzer off, LED blinks medium (500ms)

#### 3. Buzzer Pattern
- **Active in**: ALERT state only
- **Pattern**: 3 beeps, 1 second pause, repeat
  - Beep 1: 100ms ON
  - Pause: 100ms OFF
  - Beep 2: 100ms ON
  - Pause: 100ms OFF
  - Beep 3: 100ms ON
  - Long pause: 1000ms OFF
- **Total cycle**: 1500ms

#### 4. Button Behavior
- **Action**: Hold button for 1 second
- **Effect**: Transitions from ALERT → STOPPED
- **Debouncing**: 50ms debounce to prevent false triggers
- **Note**: Can also be acknowledged via BLE ACK

#### 5. Serial Debug Output
- **Frequency**: Every 2 seconds in NORMAL mode
- **Format**:
  ```
  Water Sensor: 412 | Threshold: 500 | Status: WET | State: NORMAL
  Water Sensor: 892 | Threshold: 500 | Status: DRY | State: NORMAL
  ```

---

## Configuration Parameters (config.h)

### Water Detection Tuning

```cpp
#define WATER_THRESHOLD_DEFAULT 500      // ADC threshold (0-1023)
                                         // Lower = more sensitive
                                         // Higher = less sensitive

#define WATER_SAMPLE_COUNT 5             // Rolling average samples
                                         // Higher = more stable, slower response
                                         // Lower = faster response, more noise

#define WATER_DETECTION_DEBOUNCE_MS 200  // Confirmation time
                                         // Higher = more reliable, slower
                                         // Lower = faster, more false positives
```

### Button Tuning

```cpp
#define BUTTON_HOLD_TIME_MS 1000         // Hold time for acknowledgment
#define BUTTON_DEBOUNCE_MS 50            // Debounce time
```

### Buzzer Tuning

```cpp
#define BUZZER_BEEP_DURATION_MS 100      // Each beep duration
#define BUZZER_BEEP_PAUSE_MS 100         // Pause between beeps
#define BUZZER_BEEPS_PER_SEQUENCE 3      // Number of beeps
#define BUZZER_SEQUENCE_PAUSE_MS 1000    // Pause between sequences
```

---

## Tuning the ITO Trace Sensor

### Step 1: Baseline Reading (Dry)

1. Upload firmware to XIAO nRF52
2. Open Serial Monitor (115200 baud)
3. Ensure ITO traces are completely dry
4. Observe readings for 30 seconds

**Expected dry readings**: 700-1023 (high impedance)

```
Water Sensor: 892 | Threshold: 500 | Status: DRY | State: NORMAL
Water Sensor: 901 | Threshold: 500 | Status: DRY | State: NORMAL
Water Sensor: 887 | Threshold: 500 | Status: DRY | State: NORMAL
```

**Record**: Average dry reading = _____

### Step 2: Water Reading (Wet)

1. Apply small amount of water to ITO traces
2. Observe readings in Serial Monitor
3. Water should bridge the interlinked fingers

**Expected wet readings**: 0-500 (low impedance)

```
Water Sensor: 142 | Threshold: 500 | Status: WET | State: NORMAL
Water Sensor: 138 | Threshold: 500 | Status: WET | State: NORMAL
!!! WATER DETECTED !!!
Transitioning to ALERT mode
```

**Record**: Average wet reading = _____

### Step 3: Calculate Optimal Threshold

**Formula**:
```
Threshold = (Dry_Average + Wet_Average) / 2
```

**Example**:
- Dry average: 850
- Wet average: 200
- Threshold = (850 + 200) / 2 = 525

**Adjust in config.h**:
```cpp
#define WATER_THRESHOLD_DEFAULT 525  // Your calculated value
```

### Step 4: Test Sensitivity

#### Test A: Humidity Resistance
1. Breathe on ITO traces (high humidity)
2. Sensor should NOT trigger (reading should stay above threshold)
3. If triggers: Increase threshold by 50-100

#### Test B: Small Droplet Detection
1. Apply single small droplet (~0.1ml)
2. Sensor SHOULD trigger within 1 second
3. If doesn't trigger: Decrease threshold by 50-100

#### Test C: Evaporation Response
1. Apply water, wait for alert
2. Let water evaporate naturally
3. After alert acknowledgment and reboot, sensor should return to DRY status
4. If stays WET: Increase threshold

### Step 5: Fine-Tuning Debounce

If experiencing false triggers:
```cpp
#define WATER_DETECTION_DEBOUNCE_MS 500  // Increase from 200ms
```

If detection is too slow:
```cpp
#define WATER_DETECTION_DEBOUNCE_MS 100  // Decrease from 200ms
```

---

## Testing Procedure

### Test 1: Basic Water Detection

1. Ensure sensor is dry
2. Power on module (CR2032)
3. LED should blink slowly (heartbeat)
4. Open Serial Monitor (if available)
5. Apply water to ITO traces
6. **Expected**:
   - Reading drops below threshold
   - After 200ms, "WATER DETECTED" message
   - System transitions to ALERT
   - Buzzer starts: 3 beeps, pause, repeat
   - LED blinks rapidly
   - BLE advertising switches to ALERT mode

### Test 2: Button Acknowledgment

1. While in ALERT mode (buzzing)
2. Press and hold button for 1 second
3. **Expected**:
   - "BUTTON HELD FOR 1 SECOND" message
   - System transitions to STOPPED
   - Buzzer stops
   - LED blinks at medium rate
   - BLE advertising switches to NORMAL mode
   - needs_ack flag cleared

### Test 3: BLE Acknowledgment

1. While in ALERT mode
2. Use central (RBPi) to send ACK
3. **Expected**:
   - "ACK confirmed" message
   - System transitions to STOPPED
   - Same behavior as button acknowledgment

### Test 4: Reboot After Acknowledgment

1. In STOPPED state
2. Dry the ITO traces thoroughly
3. Reset the module (power cycle)
4. **Expected**:
   - System starts in NORMAL mode
   - Monitoring resumes
   - Sequence number increments on next alert

### Test 5: False Trigger Prevention

1. System in NORMAL mode
2. Blow on ITO traces (humidity)
3. Touch traces with dry finger (capacitance)
4. **Expected**:
   - No false alert
   - Readings may fluctuate but stay above threshold

---

## Troubleshooting

### Issue: Sensor Always Reads "WET"

**Possible Causes**:
1. Threshold too high
2. ITO traces contaminated
3. Traces physically damaged/shorted

**Solutions**:
1. Check dry reading in Serial Monitor
2. If dry reading < 700, traces may be contaminated
3. Clean traces with isopropyl alcohol
4. Lower threshold if dry readings are consistently low
5. Check for physical shorts between traces

### Issue: Sensor Never Detects Water

**Possible Causes**:
1. Threshold too low
2. Poor electrical contact
3. Trace spacing too wide

**Solutions**:
1. Check wet reading in Serial Monitor
2. If wet reading > 500, water not bridging properly
3. Increase water volume or use more conductive water (add salt)
4. Decrease threshold in config.h
5. Verify pin connections (7 & 8)

### Issue: Buzzer Not Working

**Possible Causes**:
1. Wrong buzzer type (active vs passive)
2. Insufficient current drive
3. Wrong pin connections

**Solutions**:
1. Verify buzzer is 3.3V compatible
2. Check pin 5 & 6 connections
3. Test buzzer directly with 3.3V
4. Try increasing BUZZER_BEEP_DURATION_MS to 200ms

### Issue: Button Not Registering

**Possible Causes**:
1. Bouncing
2. Poor contact
3. Wrong pin connections

**Solutions**:
1. Increase BUTTON_DEBOUNCE_MS
2. Check pin 0 & 1 connections
3. Verify button is normally open (NO)
4. Test by shorting pin 0 to GND directly

### Issue: Fast Battery Drain

**Possible Causes**:
1. Buzzer running too often
2. LED always on
3. BLE advertising too frequently

**Solutions**:
1. Reduce BUZZER_BEEP_DURATION_MS
2. Verify LED is blinking, not solid
3. Check advertising intervals in config.h
4. Disable watchdog in production (non-DEBUG)

---

## Serial Debug Commands (Future Enhancement)

Consider adding serial input commands for live tuning:

```cpp
// Type in Serial Monitor:
t500  // Set threshold to 500
s10   // Set sample count to 10
d300  // Set debounce to 300ms
r     // Print current readings
c     // Print current configuration
```

**Implementation** (add to loop()):
```cpp
if (Serial.available()) {
  char cmd = Serial.read();
  if (cmd == 't') {
    water_threshold = Serial.parseInt();
    DEBUG_PRINT("Threshold set to: " + String(water_threshold));
  }
  // ... more commands
}
```

---

## Power Consumption Estimates

### Normal Mode (Monitoring)
- BLE advertising: ~1mA (1 second intervals)
- MCU active: ~5mA
- LED blink: ~0.5mA average
- **Total: ~6.5mA**
- **Battery life**: 240mAh / 6.5mA = ~37 hours

### Alert Mode (Buzzing)
- BLE advertising: ~3mA (fast intervals)
- MCU active: ~5mA
- LED blink: ~2mA average (rapid)
- Buzzer: ~10-20mA (while beeping)
- **Total: ~20-30mA**
- **Sustained alert time**: ~8-12 hours

### Stopped Mode
- BLE advertising: ~1mA
- MCU active: ~5mA
- LED blink: ~1mA (medium)
- **Total: ~7mA**

**Recommendation**: For longer battery life:
1. Reduce LED brightness
2. Shorten buzzer beep duration
3. Add sleep mode between sensor checks

---

## Deployment Checklist

### Pre-Deployment
- [ ] Tune water threshold for your ITO traces
- [ ] Test with actual water (not simulation)
- [ ] Verify buzzer is audible from expected distance
- [ ] Test button acknowledgment
- [ ] Verify BLE range to central
- [ ] Check battery voltage (should be >2.8V)

### During Demo
- [ ] Keep spare CR2032 batteries
- [ ] Have tissue/cloth to dry sensor between tests
- [ ] Monitor Serial output if possible
- [ ] Note any false positives/negatives
- [ ] Document threshold that works best

### Post-Demo Adjustments
- [ ] Review Serial logs for optimal threshold
- [ ] Adjust debounce if needed
- [ ] Consider adding hysteresis (different on/off thresholds)
- [ ] Update firmware version in code

---

## Advanced Features (Future)

### Hysteresis
Prevent oscillation between WET/DRY:
```cpp
#define WATER_THRESHOLD_LOW 450   // Trigger alert
#define WATER_THRESHOLD_HIGH 550  // Return to normal
```

### Adaptive Threshold
Learn dry baseline on boot:
```cpp
void calibrate_sensor() {
  int sum = 0;
  for (int i = 0; i < 100; i++) {
    sum += analogRead(WATER_SENSE_PIN_A);
    delay(10);
  }
  int baseline = sum / 100;
  water_threshold = baseline - 200;  // 200 below baseline
}
```

### Temperature Compensation
ITO resistance varies with temperature:
```cpp
// Read temperature from nRF52 internal sensor
// Adjust threshold based on temp
```

---

## Summary

The water sensor firmware provides:
- ✅ Real-time water detection with tunable sensitivity
- ✅ Comprehensive serial debug for optimization
- ✅ 3-state machine (Normal/Alert/Stopped)
- ✅ Buzzer alarm with distinct pattern
- ✅ Button and BLE acknowledgment
- ✅ Low power operation on CR2032
- ✅ Integration with existing BLE infrastructure

**Key to success**: Proper threshold tuning for your specific ITO traces and water conditions.

Use Serial Monitor to find the sweet spot between sensitivity and false positive prevention!
