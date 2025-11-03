# RC Timing Water Sensor Guide

## Overview

This guide covers the **RC timing-based water sensor** implementation that works with **digital-only pins** (pins 7 & 8) on the XIAO nRF52 - no ADC required!

**Firmware**: `leakseek_firmware_rc_timing.ino`

## Why RC Timing?

The RC timing method allows water detection using **digital-only pins** by measuring the time constant of an RC (resistor-capacitor) circuit:

- **No ADC needed** - works with pins 7 & 8 (digital-only)
- **No PCB rework** - uses existing 100nF capacitor
- **Clear wet/dry separation** - 100x time difference
- **Easy to tune** - adjust threshold in microseconds

## How It Works

### The Physics

The RC time constant is: **τ = R × C**

Where:
- **R** = Resistance between ITO traces (changes with water)
- **C** = 100nF capacitor (fixed, already on your PCB)
- **τ** = Time constant (how long to charge/discharge)

### The Circuit

```
Pin 7 (OUTPUT) ──────[ITO Traces (variable R)]────── Pin 8 (INPUT)
                                                         |
                                                      [100nF]
                                                         |
                                                        GND
```

### The Measurement Process

1. **Discharge**: Set pin 7 LOW, wait 10ms for full discharge
2. **Charge**: Set pin 7 HIGH, start timer
3. **Measure**: Wait for pin 8 to reach logic HIGH (~1.65V)
4. **Calculate**: Charge time = resistance indicator

### Expected Values

| Condition | Resistance | Time Constant (τ) | Charge Time (~0.5τ) | Detection |
|-----------|------------|-------------------|---------------------|-----------|
| Bone dry | 10 MΩ | 1000ms | **500ms** | DRY |
| Dry | 1 MΩ | 100ms | **50ms** | DRY |
| Humid (not wet) | 500 kΩ | 50ms | **25ms** | DRY |
| Damp | 100 kΩ | 10ms | **5ms** | WET |
| Water bridged | 10 kΩ | 1ms | **0.5ms** | WET |

**Default threshold: 20ms (20,000 microseconds)**
- Times < 20ms = **WET** (low resistance, fast charge)
- Times > 20ms = **DRY** (high resistance, slow charge)

---

## Hardware Setup

### Required Components (Already on Your PCB!)

✅ XIAO nRF52 module
✅ ITO two-trace interlinked-finger sensor
✅ 100nF capacitor from pin 8 to GND
✅ No additional components needed!

### Pin Connections

```
Pin 7: ITO Trace A (digital output for RC timing)
Pin 8: ITO Trace B (digital input, has 100nF cap to GND)
Pin 0: Button A (INPUT_PULLUP)
Pin 1: Button B (OUTPUT LOW - GND)
Pin 5: Buzzer Positive
Pin 6: Buzzer Negative
```

**No rework needed if your PCB has ITO traces on pins 7 & 8!**

---

## Firmware Configuration

### config.h Parameters

```cpp
// RC time threshold in microseconds
#define RC_TIME_THRESHOLD_US 20000    // 20ms = 20000us
                                      // Adjust based on your ITO traces

// Number of samples to average (reduces noise)
#define RC_SAMPLE_COUNT 5             // 3-10 recommended

// Inverted detection (rarely needed)
#define RC_DETECTION_INVERTED false   // false for normal operation
```

### Tuning the Threshold

**Step 1: Measure Dry Time**
1. Upload `leakseek_firmware_rc_timing.ino`
2. Open Serial Monitor (115200 baud)
3. Observe readings with sensor completely dry

```
RC Time: 85342 us | Threshold: 20000 us | Status: DRY | State: NORMAL
RC Time: 92145 us | Threshold: 20000 us | Status: DRY | State: NORMAL
```

**Dry time** = ~50-500ms (50,000-500,000 us)

**Step 2: Measure Wet Time**
1. Apply water to ITO traces
2. Observe readings when water bridges traces

```
RC Time: 2341 us | Threshold: 20000 us | Status: WET | State: NORMAL
RC Time: 1987 us | Threshold: 20000 us | Status: WET | State: NORMAL
```

**Wet time** = ~0.5-10ms (500-10,000 us)

**Step 3: Calculate Threshold**

```
Threshold = (Dry_Time + Wet_Time) / 2
```

Example:
- Dry average: 80,000 us (80ms)
- Wet average: 2,000 us (2ms)
- Threshold = (80,000 + 2,000) / 2 = **41,000 us (41ms)**

However, the **geometric mean** works better for large ranges:
```
Threshold = sqrt(Dry_Time × Wet_Time)
         = sqrt(80,000 × 2,000)
         = sqrt(160,000,000)
         = 12,649 us (~13ms)
```

**Recommended**: Use geometric mean, or simply **20ms** works for most ITO sensors.

**Update config.h**:
```cpp
#define RC_TIME_THRESHOLD_US 20000  // Your calculated value
```

---

## Graph Debug Mode

Enable graph mode for real-time visualization:

```cpp
// In config.h
#define DEBUG_MODE 2  // Graph mode (0=off, 1=text, 2=graph)
```

### Using Arduino Serial Plotter

1. Upload firmware with `DEBUG_MODE = 2`
2. Open **Tools → Serial Plotter** (not Serial Monitor!)
3. Set baud rate to **115200**

### Graph Traces

The plotter shows 6 traces (all in milliseconds for readability):

- **Raw**: Current instantaneous RC time measurement
- **Avg**: Rolling average of last 5 measurements (smooth)
- **Threshold**: Your configured threshold (horizontal line)
- **State**: System state (0=Normal, 100=Alert, 50=Stopped)
- **Upper**: Threshold + 10ms (visual margin band)
- **Lower**: Threshold - 10ms (visual margin band)

### What to Look For

**Dry sensor**:
- Raw trace: 50-500ms, somewhat noisy
- Avg trace: Stable, well above threshold
- State: 0 (Normal)

**Wet sensor**:
- Raw trace: 0.5-10ms, very fast
- Avg trace: Drops sharply below threshold
- State: 100 (Alert triggered)

**Borderline (humid but not wet)**:
- Raw trace: 15-30ms, near threshold
- May oscillate slightly
- Tune threshold or increase RC_SAMPLE_COUNT to stabilize

---

## Advantages vs ADC Method

| Feature | RC Timing | ADC Method |
|---------|-----------|------------|
| **Pins Required** | 7 & 8 (digital) | A0 & A1 (analog) |
| **PCB Rework** | ✅ None | ❌ Required (rewire to A0/A1) |
| **Measurement** | Time (microseconds) | Voltage (0-1023) |
| **Precision** | ⭐⭐⭐ Good | ⭐⭐⭐⭐ Excellent |
| **Dynamic Range** | ⭐⭐⭐⭐⭐ Huge (1000x) | ⭐⭐⭐⭐ Large |
| **Temperature Sensitivity** | ⭐⭐⭐ Moderate | ⭐⭐⭐⭐ Low |
| **Noise Immunity** | ⭐⭐⭐ Good (averaged) | ⭐⭐⭐⭐ Excellent |
| **Power Consumption** | ⭐⭐⭐⭐ Same | ⭐⭐⭐⭐ Same |
| **Complexity** | ⭐⭐⭐ Moderate | ⭐⭐ Simple |

**Bottom Line**: RC timing is excellent for this application, especially if your PCB is already wired to pins 7 & 8.

---

## Troubleshooting

### Issue: Always Reads "DRY" (Times Always >100ms)

**Possible Causes**:
1. ITO traces not connected to pins 7 & 8
2. No 100nF capacitor on pin 8
3. Capacitor value wrong (too large)
4. ITO traces physically damaged/open

**Solutions**:
1. Verify continuity from ITO traces to pins 7 & 8 with multimeter
2. Check capacitor is present and connected: pin 8 → cap → GND
3. Measure capacitor value (should be ~100nF)
4. Check ITO traces for breaks

### Issue: Always Reads "WET" (Times Always <5ms)

**Possible Causes**:
1. ITO traces shorted together
2. ITO traces contaminated (salt, dirt)
3. Capacitor missing or damaged
4. Threshold set too high

**Solutions**:
1. Check resistance between ITO traces (should be >1MΩ when dry)
2. Clean traces with isopropyl alcohol, let dry completely
3. Verify 100nF cap is present and not shorted
4. Lower RC_TIME_THRESHOLD_US to 10000 (10ms) and retest

### Issue: Readings Are Noisy/Unstable

**Possible Causes**:
1. Environmental interference (WiFi, fluorescent lights)
2. Traces too close to other circuitry
3. Sample count too low
4. Temperature fluctuations

**Solutions**:
1. Increase RC_SAMPLE_COUNT to 10 for more averaging
2. Add physical shielding if possible
3. Increase debounce time: `WATER_DETECTION_DEBOUNCE_MS 500`
4. Use hysteresis (different on/off thresholds)

### Issue: Times Are Longer Than Expected (>1 second)

**Possible Causes**:
1. Capacitor value wrong (1μF instead of 100nF?)
2. Very high resistance (>10MΩ)
3. Trace coating/contamination

**Solutions**:
1. Verify capacitor is 100nF (code 104), not 1μF (105)
2. Check dry resistance with multimeter (should be 1-10MΩ)
3. Clean traces thoroughly

### Issue: Water Detection Delayed

**Cause**: Debounce time + sample averaging

**Solution**:
```cpp
#define RC_SAMPLE_COUNT 3              // Reduce from 5 (faster response)
#define WATER_DETECTION_DEBOUNCE_MS 100  // Reduce from 200ms
```

Trade-off: Faster response, but more false positives.

---

## Testing Procedure

### Test 1: Basic RC Timing Measurement

1. Upload `leakseek_firmware_rc_timing.ino`
2. Open Serial Monitor (115200 baud)
3. Ensure sensor is dry
4. Observe readings:

**Expected**:
```
RC Time: 85342 us | Threshold: 20000 us | Status: DRY | State: NORMAL
```

**Verify**: Time is 50,000-500,000 us (50-500ms)

### Test 2: Water Detection

1. Apply water to ITO traces
2. Observe readings change

**Expected**:
```
RC Time: 2341 us | Threshold: 20000 us | Status: WET | State: NORMAL
!!! WATER DETECTED !!!
Entered ALERT mode - sequence 1
```

**Verify**:
- Time drops to 500-10,000 us (0.5-10ms)
- Alert triggers after debounce period (200ms)
- Buzzer starts beeping (3-beep pattern)
- LED blinks rapidly

### Test 3: Threshold Tuning

1. Enable graph mode (`DEBUG_MODE = 2`)
2. Open Serial Plotter
3. Observe dry readings for 30 seconds
4. Apply water, observe wet readings
5. Calculate threshold between dry and wet ranges
6. Update RC_TIME_THRESHOLD_US in config.h

### Test 4: Button Acknowledgment

1. While in ALERT mode (buzzing)
2. Press and hold button for 1 second
3. **Expected**:
   - "BUTTON HELD FOR 1 SECOND - ACKNOWLEDGING ALERT"
   - System transitions to STOPPED
   - Buzzer stops
   - LED blinks at medium rate

### Test 5: False Positive Prevention

1. System in NORMAL mode
2. Blow on ITO traces (high humidity)
3. **Expected**: No false alert (time should stay >20ms)

If false alerts occur:
- Increase threshold (e.g., 30,000 us)
- Increase debounce time (e.g., 500ms)
- Increase sample count (e.g., 10)

---

## Advanced Configuration

### Hysteresis (Prevent Oscillation)

To prevent rapid on/off toggling as water evaporates:

```cpp
// Dual threshold approach (modify in firmware)
#define RC_TIME_THRESHOLD_WET_US 15000   // Trigger alert (shorter = wet)
#define RC_TIME_THRESHOLD_DRY_US 25000   // Return to normal (longer = dry)

// Logic:
// - Go to ALERT when time drops below 15ms
// - Return to NORMAL when time exceeds 25ms
// - Creates 10ms "dead zone" to prevent oscillation
```

### Temperature Compensation

RC time varies with temperature (resistivity changes). For critical applications:

```cpp
// Read nRF52 internal temperature sensor
int32_t temp_raw;
sd_temp_get(&temp_raw);
float temp_c = temp_raw / 4.0;  // Temperature in Celsius

// Adjust threshold based on temperature
// Typical: ~2% change per 10°C
float temp_factor = 1.0 + (temp_c - 25.0) * 0.002;
unsigned long adjusted_threshold = RC_TIME_THRESHOLD_US * temp_factor;
```

### Dynamic Calibration

Auto-calibrate on boot (learn dry baseline):

```cpp
void calibrate_sensor() {
  Serial.println("Calibrating... ensure sensor is DRY");
  delay(2000);

  unsigned long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += measure_rc_time();
    delay(100);
  }

  unsigned long dry_baseline = sum / 20;
  unsigned long threshold = dry_baseline / 4;  // 25% of dry time

  Serial.print("Calibrated: Dry=");
  Serial.print(dry_baseline);
  Serial.print(" us, Threshold=");
  Serial.print(threshold);
  Serial.println(" us");
}
```

Call in `setup()` before entering main loop.

---

## Performance Comparison

### Timing Precision

The nRF52840 runs at 64 MHz, giving theoretical resolution of ~15ns. The `micros()` function provides 1μs resolution, which is more than adequate:

- **Dry time**: 50,000-500,000 us → Resolution: 0.002-0.02%
- **Wet time**: 500-10,000 us → Resolution: 0.01-0.2%
- **Threshold**: 20,000 us → Resolution: 0.005%

**Conclusion**: Timing resolution is not a limiting factor.

### Response Time

**Total detection latency**:
1. RC measurement: 0.5-500ms (depends on resistance)
2. Sample averaging: 5 measurements × loop delay = 500ms
3. Debounce: 200ms
4. **Total**: ~0.7-1.2 seconds (typical: ~1s)

**Faster response** (reduce to ~300ms):
```cpp
#define RC_SAMPLE_COUNT 3
#define WATER_DETECTION_DEBOUNCE_MS 50
#define LOOP_DELAY_MS 50
```

**More reliable** (increase to ~2s):
```cpp
#define RC_SAMPLE_COUNT 10
#define WATER_DETECTION_DEBOUNCE_MS 500
```

---

## Power Consumption

RC timing has similar power consumption to ADC method:

### Measurement Power

- **Discharge phase** (10ms): Pin 7 drives LOW, ~1mA
- **Charge phase** (0.5-500ms): Pin 7 drives HIGH through ITO resistance
  - Dry (1MΩ): 3.3V / 1MΩ = **3.3 μA**
  - Wet (10kΩ): 3.3V / 10kΩ = **330 μA**
- **Measurement frequency**: Every 100ms (10 Hz)

**Average power from RC timing**:
- Dry: Negligible (<10 μA)
- Wet: ~30 μA average (during measurement)

**Total system power** (same as ADC version):
- Normal mode: ~6.5 mA
- Alert mode: ~20-30 mA (buzzer dominates)
- **Battery life**: ~2-4 months on CR2032

---

## Comparison with Other Digital Methods

### RC Timing vs Capacitive Touch

| Method | RC Timing | Capacitive Touch |
|--------|-----------|------------------|
| Hardware | 100nF cap (have it!) | Special sensor pads |
| Firmware | Simple timing loop | Complex nRF SDK |
| Water Detection | ✅ Excellent | ⚠️ Marginal |
| Touch Detection | ❌ No | ✅ Excellent |
| Power | Low | Very low |
| Tuning | Easy (threshold in μs) | Complex (registers) |

**Verdict**: RC timing is better for water detection.

---

## Summary

The RC timing water sensor method is an excellent solution for your XIAO nRF52 PCB:

✅ **No PCB rework** - uses existing pins 7 & 8
✅ **No additional components** - 100nF cap already there
✅ **Clear wet/dry separation** - 100x time difference
✅ **Easy to tune** - single threshold parameter
✅ **Good precision** - microsecond resolution
✅ **Low power** - similar to ADC method
✅ **Robust** - averaged and debounced

**Recommended Configuration**:
```cpp
#define RC_TIME_THRESHOLD_US 20000      // 20ms threshold
#define RC_SAMPLE_COUNT 5               // 5-sample average
#define WATER_DETECTION_DEBOUNCE_MS 200 // 200ms debounce
```

Upload `leakseek_firmware_rc_timing.ino` and it should work immediately with your existing hardware!
