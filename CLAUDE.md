# LeakSeek System - Claude Code Reference

## Mission Critical Context
**This is a safety-critical system for marine leak detection. A failure means a vessel might sink.**

## System Architecture Overview

### Hardware Components
- **nRF52 Modules**: BLE peripherals with leak sensors (Arduino-based firmware)
- **Raspberry Pi Central**: BLE scanner/central running Python (BlueZ stack)
- **E-ink Display**: Status display on RBPi (Waveshare)
- **Web Interface**: FastAPI + static HTML/JS

### Current Implementation (v2)
**Advertisement-based monitoring** - Industry standard approach:

```
┌─────────────────────────────┐
│  nRF52 Module               │
│  ┌─────────────────────┐    │
│  │ Normal Mode:        │    │
│  │ • Non-connectable   │    │
│  │ • 1000ms intervals  │    │
│  │ • Mfg data status   │    │
│  └─────────────────────┘    │
│  ┌─────────────────────┐    │
│  │ Alert Mode:         │    │
│  │ • Connectable       │    │
│  │ • 20ms (5s burst)   │    │
│  │ • Then 100ms        │    │
│  │ • Wait for ACK      │    │
│  └─────────────────────┘    │
└─────────────────────────────┘
         │ Advertising
         ↓
┌─────────────────────────────┐
│  RBPi Central               │
│  ┌─────────────────────┐    │
│  │ Continuous scan     │    │
│  │ Parse mfg data      │    │
│  │ Update status       │    │
│  │ Queue ACKs          │    │
│  └─────────────────────┘    │
│  ┌─────────────────────┐    │
│  │ ACK on alert:       │    │
│  │ • Connect           │    │
│  │ • Write seq #       │    │
│  │ • Disconnect        │    │
│  └─────────────────────┘    │
└─────────────────────────────┘
```

## Key Files

### Firmware (nRF52)
- `leakseek_firmware/leakseek_firmware_v2.ino` - Main firmware (CURRENT)
- `leakseek_firmware/config.h` - Configuration constants
- `leakseek_firmware/leakseek_firmware.ino` - Old v1 firmware (archived)

### Server (RBPi)
- `server/central_v2.py` - Main central code (CURRENT)
- `server/eink_display.py` - E-ink display management
- `server/utils.py` - Device registration storage
- `server/central.py` - Old v1 central (archived)

### Web Interface
- `web/index.html`, `web/app.js`, `web/api.js` - Web UI
- Works with both v1 and v2 (same API)

## Known Issues & Workarounds

### RBPi Bluetooth Flakiness
**Problem**: RBPi (especially Zero 2 W) has notoriously unreliable Bluetooth
**Current Mitigations**:
- 8-second scan windows (longer = more stable)
- 10-second connection timeout (up from 5s)
- Semaphore limiting to 1 concurrent connection
- Auto-recovery in scanner loop
- Pause scanning during ACK operations

### E-ink Systemd Timing Issue
**Problem**: E-ink fails when started by systemd at boot
**Root Cause**: Driver detection happens before gpiomem-bcm2835 loads
**Fix**: Force RaspberryPi mode in `epdconfig.py:310-317`
```python
# Force RaspberryPi (systemd timing workaround)
implementation = RaspberryPi()
```

### Advertising Update Bug (FIXED)
**Was**: Calling `addData()` every loop appended to buffer → overflow
**Now**: Only rebuild advertising when data actually changes

### GATT Indication Flooding (FIXED)
**Was**: Sending indication every second regardless of state change
**Now**: Only send when state changes

## Critical Configuration Parameters

### Timing Constants (config.h)
```cpp
#define ADV_INTERVAL_NORMAL 1600      // 1000ms between ads (normal)
#define ADV_INTERVAL_ALERT_FAST 32    // 20ms (fast burst)
#define ADV_INTERVAL_ALERT_SLOW 160   // 100ms (after burst)
#define LOOP_DELAY_MS 1000            // Sensor check interval
#define FAST_ADV_DURATION 5000        // 5s of fast advertising
#define INCIDENT_COOLDOWN_MS 15000    // 15s between incidents
```

### Central Parameters (central_v2.py)
```python
STALE_THRESHOLD_SECONDS = 20          # When to mark device as stale
timeout=10.0                          # BleakClient connection timeout
await scanner(8)                      # Scan window duration
max_retries=2                         # ACK retry attempts
```

## Manufacturer Data Protocol

### Advertisement Format
```
Offset  Size  Field           Example
------  ----  --------------  -------
0-1     2     Manufacturer ID 0x8B01 (Konica Minolta)
2       1     Protocol Ver    0x01
3       1     Flags           0x03 (leak + needs_ack)
4       1     Sequence #      0x05
5       1     Battery %       0x64 (100)

Flags bits:
  Bit 0: Leak detected
  Bit 1: Needs ACK
  Bits 2-7: Reserved
```

## Robustness Concerns & Improvement Areas

### 1. Scanner Reliability (HIGH PRIORITY)
**Current**: Basic try/catch with fixed retry
**Issues**:
- No exponential backoff
- No Bluetooth adapter health checks
- No automatic adapter reset on persistent failures
- Could get stuck if BlueZ wedges

**Improvements Needed**:
- Exponential backoff with jitter
- Bluetooth adapter health monitoring
- Automatic `hciconfig hci0 reset` on persistent failures
- Watchdog timer to detect scanner hangs

### 2. ACK Reliability (HIGH PRIORITY)
**Current**: 2 retries with 1s fixed delay
**Issues**:
- Fixed retry delay doesn't account for congestion
- No tracking of ACK success rate
- Lost ACKs if device goes out of range
- No persistent queue across restarts

**Improvements Needed**:
- Exponential backoff for retries
- Persistent ACK queue (SQLite or JSON)
- Success rate tracking and alerting
- RSSI check before ACK attempt

### 3. Alert Deduplication (MEDIUM PRIORITY)
**Current**: Sequence number resets on power cycle
**Issues**:
- Same alert could be processed twice after module reboot
- No alert history persistence

**Improvements Needed**:
- Persistent sequence tracking per device
- Alert history with timestamps
- Duplicate detection across power cycles

### 4. Health Monitoring (HIGH PRIORITY)
**Current**: None
**Issues**:
- No visibility into system health
- Can't detect degraded performance
- No alerts for high failure rates

**Improvements Needed**:
- Metrics collection (scan rate, ACK success, errors)
- Health check endpoint
- Automatic alerts on degraded performance
- Prometheus-compatible metrics export

### 5. Signal Quality (MEDIUM PRIORITY)
**Current**: Accepts all advertisements regardless of RSSI
**Issues**:
- Might try to ACK devices with weak signal
- No distance-based filtering

**Improvements Needed**:
- RSSI threshold filtering
- Track RSSI trends per device
- Warn on weak signals

### 6. Power Management (MEDIUM PRIORITY)
**Current**: nRF52 runs at full power
**Issues**:
- Battery life could be better
- No sleep modes used

**Improvements Needed**:
- Enable SD_POWER sleep in loop
- Reduce TX power when not in alert
- Watchdog timer for crash recovery

### 7. Connection Concurrency (LOW PRIORITY)
**Current**: Semaphore limits to 1 ACK at a time
**Issues**:
- Multiple alerts = serial processing
- Could be slow with many devices

**Improvements Needed**:
- Consider allowing 2-3 concurrent ACKs
- Priority queue (older alerts first)

### 8. Error Recovery (MEDIUM PRIORITY)
**Current**: Basic recovery in scanner loop
**Issues**:
- E-ink failures just log, but continue to retry
- No circuit breaker pattern
- No graceful degradation

**Improvements Needed**:
- Circuit breaker for e-ink (stop trying after N failures)
- Graceful degradation modes
- Better error categorization (transient vs permanent)

## Testing Strategy

### Unit Tests Needed
- Manufacturer data parser
- Sequence number tracking
- Stale device detection
- ACK retry logic

### Integration Tests
- End-to-end leak detection flow
- ACK under high load (many devices)
- Recovery from Bluetooth failures
- Long-running stability test (24+ hours)

### Stress Tests
- 10+ devices advertising simultaneously
- Rapid alert bursts
- Network congestion scenarios
- RBPi resource exhaustion

## Deployment Checklist

### Pre-deployment
- [ ] Upload firmware to all modules
- [ ] Verify e-ink display works with systemd
- [ ] Test web interface connectivity
- [ ] Verify all modules appear in scan
- [ ] Test ACK flow for each module

### Day-of-demo
- [ ] Power cycle RBPi (fresh start)
- [ ] Verify systemd service running
- [ ] Check e-ink display updating
- [ ] Trigger one test alert and ACK
- [ ] Monitor logs for errors

### Post-deployment Monitoring
- [ ] Check ACK success rate
- [ ] Monitor scanner uptime
- [ ] Review error logs daily
- [ ] Track battery levels

## Common Debugging Commands

```bash
# Check Bluetooth status
sudo hciconfig hci0

# Reset Bluetooth adapter
sudo hciconfig hci0 down
sudo hciconfig hci0 up

# View service logs
sudo journalctl -u leakseek -f

# Test e-ink manually
cd server
python test_eink.py

# Manual scan for devices
sudo bluetoothctl
scan on

# Check API
curl http://localhost:8000/sensor_data
curl http://localhost:8000/discovered_devices
```

## Performance Targets

### Latency
- Alert detection: < 2 seconds
- ACK completion: < 5 seconds
- Web UI update: < 3 seconds

### Reliability
- ACK success rate: > 95%
- Scanner uptime: > 99.9%
- False positive rate: < 0.1%

### Scalability
- Support 50+ sensors
- < 10% CPU usage on RBPi
- < 200MB memory usage

## Critical Code Paths

### Alert Flow (Firmware)
1. `loop()` detects leak (line 78)
2. Sets `current_flags = 0x03` (line 82)
3. Calls `set_advertising_mode(ADV_MODE_ALERT)` (line 83)
4. Rebuilds advertisement (line 107-138)
5. Waits for ACK via `ack_write_callback()` (line 240)

### Alert Flow (Central)
1. `detection_callback()` receives advertisement (line 103)
2. `parse_manufacturer_data()` extracts flags (line 61-81)
3. Checks `needs_ack` flag (line 132)
4. Enqueues `ack_device()` task (line 136)
5. `ack_device()` connects and writes ACK (line 141-182)

## Refactoring Priorities

### Phase 1: Reliability (IMMEDIATE)
1. Exponential backoff for scanner
2. Bluetooth adapter health checks
3. ACK success rate tracking
4. Watchdog timer

### Phase 2: Robustness (WEEK 1)
1. Persistent ACK queue
2. Alert history with deduplication
3. RSSI filtering
4. Circuit breaker for e-ink

### Phase 3: Monitoring (WEEK 2)
1. Metrics endpoint
2. Health dashboard
3. Automatic alerting
4. Performance profiling

### Phase 4: Optimization (MONTH 1)
1. Power management on nRF52
2. Concurrent ACK handling
3. Reduced e-ink updates
4. Memory optimization

## Contact Points

### If Bluetooth Fails
1. Check `sudo systemctl status bluetooth`
2. Try `sudo systemctl restart bluetooth`
3. Last resort: `sudo reboot`

### If E-ink Freezes
1. Check permissions: `groups | grep gpio`
2. Verify library: `ls e-Paper/RaspberryPi_JetsonNano/python/lib/`
3. Test standalone: `python server/test_eink.py`

### If Web UI Unresponsive
1. Check API: `curl http://localhost:8000/sensor_data`
2. Check service: `sudo systemctl status leakseek`
3. Check logs: `sudo journalctl -u leakseek -n 100`

## Version History

- **v2.0** (Oct 2024): Advertisement-based architecture
  - Manufacturer data broadcasting
  - ACK-only connections
  - E-ink display support

- **v1.0** (Initial): Persistent connection model
  - Continuous GATT connections
  - Indication-based updates
  - Limited to 3-5 devices

## Next Steps

See TODO list for current improvement tasks.
