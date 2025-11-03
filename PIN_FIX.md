# XIAO nRF52 Pin Assignment Fix - Water Sensor

## Problem Summary

The water sensor firmware was reading **0 for all analog values** from pins 7 & 8, making water detection impossible.

## Root Cause

**Pins 7 & 8 on the XIAO nRF52 are DIGITAL-ONLY pins** and do not support analog input (ADC).

The firmware was using `analogRead(7)` which always returned 0 because pin 7 has no ADC capability.

## XIAO nRF52 Analog Pin Reference

Only the following pins support analog input on the XIAO nRF52:

| Pin Label | Arduino Name | ADC Capable | Notes |
|-----------|--------------|-------------|-------|
| A0 | A0 or 2 | ✅ YES | Recommended for ITO sensor |
| A1 | A1 or 3 | ✅ YES | Recommended for ITO sensor |
| A2 | A2 or 4 | ✅ YES | |
| A3 | A3 or 5 | ✅ YES | |
| A4 | A4 or 28 | ✅ YES | |
| A5 | A5 or 29 | ✅ YES | |
| 7 | 7 | ❌ NO | Digital only! |
| 8 | 8 | ❌ NO | Digital only! |

## Solution Implemented

### Firmware Changes

Updated `config.h` pin assignments:

**BEFORE (BROKEN):**
```cpp
#define WATER_SENSE_PIN_A 7   // Digital only - doesn't work!
#define WATER_SENSE_PIN_B 8   // Digital only - doesn't work!
```

**AFTER (FIXED):**
```cpp
#define WATER_SENSE_PIN_A A0   // Analog capable - works!
#define WATER_SENSE_PIN_B A1   // Analog capable - works!
```

### Hardware Changes Required

**If PCB already fabricated with pins 7 & 8:**

You have three options:

#### Option 1: Jumper Wire Rework (Quick Fix)
1. Cut traces from ITO sensor to pins 7 & 8
2. Solder jumper wires from ITO sensor to A0 & A1
3. Move 100nF capacitor from pin 8 to pin A1

#### Option 2: Cut and Jump on PCB
1. Use exacto knife to cut traces to pins 7 & 8
2. Scrape solder mask on A0 & A1 pads
3. Solder thin wire jumpers from cut traces to A0 & A1
4. Update capacitor placement to A1

#### Option 3: PCB Redesign
1. Update PCB design files to route ITO traces to A0 & A1
2. Ensure 100nF cap connects from A1 to GND
3. Re-fabricate PCB with correct pin assignments

## Testing the Fix

### Step 1: Upload Diagnostic Firmware

Upload `pin_diagnostic.ino` to test the new pin assignments:

```bash
# Expected output if wired correctly:
Pin A0 & A1 Specific Test (CORRECT PINS)
Configuration: Pin A0 INPUT, Pin A1 OUTPUT LOW
  Pin A0 reading: <some value, NOT 0>
Configuration: Pin A0 INPUT, Pin A1 OUTPUT HIGH
  Pin A0 reading: <different value>
✅ WORKING: Pin A0 responds to pin A1 changes
```

### Step 2: Verify Readings in Live Mode

The diagnostic tool will continuously print:
```
Live Pin A0: 512  Pin A1: LOW
Live Pin A0: 892  Pin A1: HIGH
```

Readings should change and NOT be stuck at 0.

### Step 3: Upload Water Sensor Firmware

Once diagnostic confirms pins work, upload `leakseek_firmware_water_sensor.ino`.

Enable graph debug mode for tuning:
```cpp
// In config.h
#define DEBUG_MODE 2  // Graph mode
```

Open Serial Plotter and verify:
- **Raw** trace shows real-time readings (should fluctuate, NOT stay at 0)
- **Avg** trace shows smoothed average
- Readings change when you touch/connect the ITO traces

## Troubleshooting After Pin Change

### Issue: Still Reading 0

**Cause**: PCB still wired to pins 7 & 8, not A0 & A1

**Solution**:
1. Use multimeter to verify ITO traces connect to A0 & A1 on XIAO
2. Check for broken solder joints
3. Verify firmware is using updated config.h with A0/A1 definitions

### Issue: Readings Are Noisy

**Cause**: Missing or incorrectly placed filter capacitor

**Solution**:
1. Verify 100nF capacitor is connected from A1 to GND
2. Capacitor should be physically close to XIAO module
3. If excessive noise, try increasing capacitance (220nF or 470nF)

### Issue: Readings Don't Change Between Dry/Wet

**Cause**:
- ITO traces not properly connected
- Trace spacing too wide
- Threshold incorrectly set

**Solution**:
1. Bridge ITO traces with wet finger - reading should change significantly
2. Check Serial Plotter: dry reading should be ~700-1000, wet ~100-500
3. If reversed (wet is higher), set `WATER_DETECTION_INVERTED true` in config.h

## Updated Pin Mapping Summary

### Working Configuration

```
Component            | Pin(s)        | Type    | Notes
---------------------|---------------|---------|------------------------
ITO Water Sensor IN  | A0            | Analog  | MUST be analog-capable!
ITO Water Sensor REF | A1            | Analog  | Has 100nF cap to GND
Button A             | D0            | Digital | INPUT_PULLUP
Button B             | D1            | Digital | OUTPUT LOW (GND)
Buzzer Positive      | D5            | Digital | OUTPUT
Buzzer Negative      | D6            | Digital | OUTPUT LOW (GND)
```

## Impact on Existing Deployments

If you have already fabricated PCBs with the old pin assignments:

**Prototype/Dev boards**: Use jumper wire rework (Option 1)
**Production boards**: Recommend PCB redesign (Option 3) for reliability

## Files Updated

1. **leakseek_firmware/config.h** - Pin definitions changed to A0/A1
2. **leakseek_firmware/pin_diagnostic.ino** - Updated to test A0/A1
3. **WATER_SENSOR_GUIDE.md** - Added critical pin warning
4. **PIN_FIX.md** - This document

## Verification Checklist

Before deploying updated firmware:

- [ ] PCB hardware verified: ITO traces connect to A0 & A1 (NOT 7 & 8)
- [ ] 100nF capacitor verified: Connected from A1 to GND
- [ ] pin_diagnostic.ino test passed: A0 readings are NOT 0
- [ ] pin_diagnostic.ino test passed: A0 readings change with A1 HIGH/LOW
- [ ] Water sensor firmware uploaded with DEBUG_MODE = 2
- [ ] Serial Plotter shows varying readings (not stuck at 0)
- [ ] Dry sensor: readings 700-1000
- [ ] Wet sensor: readings 100-500 (or inverted)
- [ ] Water threshold tuned appropriately

## Lessons Learned

1. **Always check datasheet pin capabilities** before PCB design
2. **Not all GPIO pins support ADC** on microcontrollers
3. **Use labeled analog pins (A0-A5)** for analog inputs, even if numbered pins exist
4. **Test pin functionality early** with diagnostic firmware
5. **Document hardware constraints prominently** in guides

## References

- XIAO nRF52 Pinout: https://wiki.seeedstudio.com/XIAO_BLE/
- nRF52840 Datasheet: Only certain pins have SAADC (analog) capability
- Arduino `analogRead()`: Requires ADC-capable pin, returns 0 otherwise (no error!)
