# LeakSeek Firmware

This directory contains firmware for the XIAO nRF52 water sensor module.

## Current Firmware Files

### Water Sensor Implementations

**Choose ONE based on your hardware:**

#### 1. `leakseek_firmware_rc_timing.ino` (RECOMMENDED)
- **For PCBs with ITO traces on pins 7 & 8**
- Uses RC time constant measurement (digital pins only)
- **No PCB rework needed!**
- Works with existing 100nF capacitor
- Detection: Fast charge (<20ms) = wet, slow charge (>50ms) = dry
- See: `RC_TIMING_GUIDE.md` for details

#### 2. `leakseek_firmware_water_sensor.ino` (Alternative)
- **Requires PCB rework to connect ITO traces to A0 & A1**
- Uses ADC (analog-to-digital converter)
- Slightly better precision than RC timing
- Detection: Low voltage (<threshold) = wet, high voltage = dry
- See: `WATER_SENSOR_GUIDE.md` for details

### Configuration

#### 3. `config.h`
- Shared configuration for all firmware
- Pin assignments
- Thresholds and timing parameters
- BLE settings
- Debug modes

## Quick Start

### For Existing PCBs (pins 7 & 8):

1. Upload `leakseek_firmware_rc_timing.ino`
2. Open Serial Monitor (115200 baud) or Serial Plotter
3. Test with water on ITO traces
4. Tune `RC_TIME_THRESHOLD_US` in `config.h` if needed

### For New PCBs (pins A0 & A1):

1. Ensure ITO traces connected to A0 & A1 (NOT 7 & 8!)
2. Upload `leakseek_firmware_water_sensor.ino`
3. Open Serial Monitor or Serial Plotter
4. Test with water on ITO traces
5. Tune `WATER_THRESHOLD_DEFAULT` in `config.h` if needed

### Troubleshooting:

Refer to XIAO nRF52 schematics for pin capabilities:
- A0-A5: Analog-capable (ADC)
- Other pins: Digital only

## Documentation

- **CLAUDE.md** - System architecture and reference
- **RC_TIMING_GUIDE.md** - RC timing implementation details
- **WATER_SENSOR_GUIDE.md** - ADC water sensor details
- **PIN_FIX.md** - Pin assignment issue and solutions
- **ROBUSTNESS_IMPROVEMENTS.md** - v2.1 reliability improvements
- **STALE_DEVICE_FIX.md** - RBPi Zero W 2 constraints

## Firmware Versions

- **v2.3** - RC timing water sensor (current)
- **v2.2** - ADC water sensor (current)
- **v2.1** - Production reliability improvements
- **v1.0** - Original (obsolete, removed)

## Hardware Requirements

- XIAO nRF52 module (Seeed Studio)
- CR2032 coin cell (3.3V)
- ITO two-trace interlinked-finger sensor
- 100nF capacitor (pin 8 or A1 to GND)
- Pushbutton (pins 0 & 1)
- 3.3V Buzzer (pins 5 & 6)

## Pin Assignments

### RC Timing Firmware (pins 7 & 8):
```
Pin 7: ITO Trace A (digital output)
Pin 8: ITO Trace B (digital input, 100nF cap to GND)
Pin 0: Button A (INPUT_PULLUP)
Pin 1: Button B (OUTPUT LOW)
Pin 5: Buzzer Positive
Pin 6: Buzzer Negative
```

### ADC Firmware (pins A0 & A1):
```
Pin A0: ITO Trace A (analog input)
Pin A1: ITO Trace B (reference, 100nF cap to GND)
Pin 0:  Button A (INPUT_PULLUP)
Pin 1:  Button B (OUTPUT LOW)
Pin 5:  Buzzer Positive
Pin 6:  Buzzer Negative
```

## Serial Debug Modes

Set in `config.h`:

```cpp
#define DEBUG_MODE 0  // No debug output
#define DEBUG_MODE 1  // Text debug (Serial Monitor)
#define DEBUG_MODE 2  // Graph debug (Serial Plotter)
```

### Text Mode (DEBUG_MODE = 1):
```
RC Time: 85342 us | Threshold: 20000 us | Status: DRY | State: NORMAL
Water Sensor: 892 | Threshold: 500 | Status: DRY | State: NORMAL
```

### Graph Mode (DEBUG_MODE = 2):
Open Serial Plotter to see real-time graphs:
- Raw readings
- Averaged values
- Threshold line
- System state

## BLE Protocol

All firmware uses the same BLE advertising protocol:

**Manufacturer Data (6 bytes):**
```
[0-1]: Manufacturer ID (0x018B = Konica Minolta)
[2]:   Protocol version (1)
[3]:   Flags (bit0=leak, bit1=needs_ack)
[4]:   Sequence number
[5]:   Battery percent (0-100)
```

**Alert Modes:**
- Normal: Non-connectable, 1000ms interval
- Alert: Connectable, 20ms fast / 100ms slow

## System States

All firmware implements the same state machine:

1. **NORMAL** - Monitoring for water, LED slow blink (2s)
2. **ALERT** - Water detected, buzzer active, LED fast blink (100ms)
3. **STOPPED** - Alert acknowledged, buzzer off, LED medium blink (500ms)

**Transitions:**
- NORMAL → ALERT: Water detected for >200ms
- ALERT → STOPPED: Button held 1s OR BLE ACK received
- STOPPED → NORMAL: Dry sensor + reboot module

## Acknowledgment Methods

1. **Button**: Hold button for 1 second
2. **BLE**: Central sends 0x01 to ACK characteristic

## Battery Life

- Normal mode: ~6.5 mA → ~2-4 months on CR2032
- Alert mode: ~20-30 mA (buzzer) → ~8-12 hours sustained

## Development Notes

- Use Seeeduino nRF52 board package v1.1.10+
- Arduino IDE 1.8.x or 2.x
- Enable verbose compilation for debugging
- Hardware watchdog: 4s timeout (disabled in DEBUG mode)
- Firmware includes version string in BLE device info

## Migration from Older Versions

If upgrading from v1.0 or v2.0:
1. Old firmware files removed from repo (in git history if needed)
2. New firmware uses advertising-only model (not connection-based)
3. Central (RBPi) updated to match new protocol
4. See git commit history for migration details
