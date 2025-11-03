# LeakSeek System Testing Guide

## System Status

### ✅ What's Working
- **Firmware**: RC timing sensor functioning correctly
  - Dry baseline: ~100ms
  - Water detection: ~2-3ms
  - Threshold: 20ms (5x safety margin)
  - Responsive to water/touch events
- **Central (RBPi)**: Full v2 implementation with:
  - Advertisement-based monitoring
  - ACK with exponential backoff
  - Bluetooth health checks
  - Metrics tracking
  - Health monitoring endpoint
  - RSSI filtering
  - Alert deduplication
- **Web Interface**: Available and configured
- **E-ink Display**: Configured with systemd workaround

### 📋 Pre-Test Checklist

#### Hardware Setup
- [ ] nRF52 module powered (CR2032 battery or USB)
- [ ] Raspberry Pi powered and connected to network
- [ ] E-ink display connected to RPi GPIO

#### Firmware Verification
- [ ] Latest firmware uploaded (commit `bd09ecc` or later)
- [ ] Serial monitor shows graph output (RAW/AVG values)
- [ ] Dry sensor reads ~100ms baseline
- [ ] Wet sensor (water drop) reads ~2-3ms

#### Central Verification
- [ ] RBPi has network connectivity
- [ ] Bluetooth adapter is UP: `sudo hciconfig hci0`
- [ ] Python dependencies installed: `pip install -r server/requirements.txt`

---

## Test Plan: End-to-End System

### Test 1: Basic Discovery
**Goal**: Verify central can discover and track the nRF52 module

1. **Start the central**:
   ```bash
   cd server
   python central_v2.py
   ```

2. **Expected output**:
   ```
   INFO - Discovered device: LeakSeek, XX:XX:XX:XX:XX:XX, RSSI=-XX
   ```

3. **Verify via API**:
   ```bash
   curl http://localhost:8000/discovered_devices
   ```
   Should return JSON with your device

**✅ Pass Criteria**: Device appears in discovered devices list within 10 seconds

---

### Test 2: Normal Monitoring (No Alert)
**Goal**: Verify sensor data is being received and tracked

1. **Keep sensor DRY** (remove any water)

2. **Check sensor data**:
   ```bash
   curl http://localhost:8000/sensor_data
   ```

3. **Expected JSON**:
   ```json
   {
     "XX:XX:XX:XX:XX:XX": {
       "value": 0,
       "seq": X,
       "battery": 100,
       "timestamp": XXXXX,
       "needs_ack": false,
       "last_seen": XXXXX,
       "rssi": -XX
     }
   }
   ```

4. **Verify**:
   - `value` should be `0` (no leak)
   - `needs_ack` should be `false`
   - `timestamp` should be recent (< 20 seconds old)

**✅ Pass Criteria**: Sensor data updates regularly, value=0, needs_ack=false

---

### Test 3: Alert Detection
**Goal**: Verify leak detection triggers alert mode

1. **Trigger leak**: Place water drop on ITO sensor traces

2. **Watch Serial Monitor**: Should show RAW dropping to 2-3ms

3. **Watch Central logs**: Should show:
   ```
   WARNING - ⚠️  ALERT from XX:XX:XX:XX:XX:XX, seq=X, RSSI=-XX, queuing ACK
   ```

4. **Check sensor data API**:
   ```bash
   curl http://localhost:8000/sensor_data
   ```
   Expected:
   - `value`: `1` (leak detected)
   - `needs_ack`: `true`

5. **Firmware behavior**:
   - Buzzer should beep (3 beeps, pause, repeat)
   - LED should blink rapidly
   - Advertising interval increases (20ms fast, then 100ms)

**✅ Pass Criteria**:
- Central detects alert within 2 seconds
- Sensor data shows value=1, needs_ack=true
- Buzzer beeping
- ACK queued in logs

---

### Test 4: ACK Flow
**Goal**: Verify central can connect and ACK the alert

1. **After alert triggers**, watch central logs for:
   ```
   INFO - ACK attempt 1/5 for XX:XX:XX:XX:XX:XX, seq=X
   INFO - ✓ ACK sent to XX:XX:XX:XX:XX:XX, seq=X, latency=X.XXs
   ```

2. **Firmware behavior after ACK**:
   - **Buzzer continues** (stays on until button pressed!)
   - Advertising returns to normal speed (1000ms)
   - Remains in ALERT state

3. **Check sensor data API**:
   ```bash
   curl http://localhost:8000/sensor_data
   ```
   Expected:
   - `needs_ack`: `false` (ACK received)
   - `last_ack_time`: recent timestamp
   - `value`: still `1` (leak present)

4. **Serial Monitor**: Should show:
   ```
   ACK received via BLE - central acknowledged, but buzzer continues
   Press button to silence buzzer and freeze sensor
   ```

**✅ Pass Criteria**:
- ACK completes within 5 seconds
- **Buzzer continues** (intentional - guides user to leak)
- needs_ack becomes false
- Firmware stays in ALERT state
- Advertising returns to normal speed

---

### Test 5: Button Silence (Primary Method)
**Goal**: Verify button silences buzzer after user locates leak

1. **Trigger alert** (water drop)

2. **Wait for central ACK** (buzzer continues)

3. **Hold button for 1 second**

4. **Expected**:
   - Serial Monitor: "BUTTON HELD FOR 1 SECOND - ACKNOWLEDGING ALERT"
   - **Buzzer stops** (finally!)
   - State changes to STOPPED
   - Sensor frozen (no more readings until reboot)

5. **To reset**: Power cycle the nRF52 module

**✅ Pass Criteria**:
- Button hold stops buzzer
- Firmware enters STOPPED state
- Sensor readings stop updating
- Power cycle returns to NORMAL operation

---

### Test 6: Health Monitoring
**Goal**: Verify health endpoint reports system status

1. **Check health endpoint**:
   ```bash
   curl http://localhost:8000/health | jq
   ```

2. **Expected response**:
   ```json
   {
     "status": "healthy",
     "watchdog": {
       "is_healthy": true,
       "last_activity": XXXXX,
       "seconds_since_activity": X.XX
     },
     "scanner": {
       "scans_started": XX,
       "scans_completed": XX,
       "success_rate": 1.0,
       "advertisements_received": XX
     },
     "ack": {
       "attempts": X,
       "successes": X,
       "success_rate": 1.0
     }
   }
   ```

3. **Verify**:
   - `status`: "healthy"
   - `scanner.success_rate`: > 0.95
   - `ack.success_rate`: > 0.90 (if any ACKs attempted)

**✅ Pass Criteria**: System reports healthy, high success rates

---

### Test 7: Web Interface
**Goal**: Verify web UI displays device status

1. **Open browser**: Navigate to `http://<rpi-ip>:8000/`

2. **Check device list**:
   - Should show discovered LeakSeek device(s)
   - Should show registration status
   - Should show current sensor state

3. **Trigger alert** and refresh:
   - Device should show alert status
   - Should show leak detected

**✅ Pass Criteria**: Web UI loads and displays device status correctly

---

### Test 8: E-ink Display (if connected)
**Goal**: Verify e-ink display updates with device status

1. **Check display**:
   - Should show device count
   - Should show status of registered devices
   - Should update when alert triggers

2. **Trigger alert**:
   - Display should update within 30 seconds
   - Should show "LEAK" or alert indicator

**✅ Pass Criteria**: E-ink display shows device status and updates on alerts

---

## Stress Tests

### Stress Test 1: Rapid Alert Cycles
**Goal**: Verify system handles repeated alerts

1. Trigger alert (water drop)
2. Wait for ACK
3. Reboot nRF52 (or remove/dry sensor)
4. Repeat 10 times

**✅ Pass Criteria**: All alerts detected and ACKed successfully

---

### Stress Test 2: Poor Signal
**Goal**: Verify RSSI filtering works

1. Move nRF52 far from RPi (weak signal)
2. Trigger alert
3. Check logs for RSSI filtering message:
   ```
   WARNING - ALERT from XX:XX:XX:XX:XX:XX (RSSI=-85) - signal too weak for ACK
   ```

**✅ Pass Criteria**: System doesn't attempt ACK if RSSI < -80dBm

---

### Stress Test 3: 24-Hour Stability
**Goal**: Verify system runs reliably for extended period

1. Start central and leave running for 24 hours
2. Periodically check health endpoint
3. Trigger a few test alerts throughout the day

**✅ Pass Criteria**:
- Scanner uptime > 99%
- No crashes or hangs
- Health endpoint always returns "healthy"

---

## Troubleshooting

### Issue: Device not discovered
**Solutions**:
1. Check Bluetooth is powered: `sudo hciconfig hci0`
2. Check nRF52 is advertising: look for LED heartbeat
3. Verify firmware uploaded correctly
4. Try Bluetooth reset: `sudo hciconfig hci0 reset`

### Issue: ACK fails repeatedly
**Solutions**:
1. Check RSSI (signal strength): `curl http://localhost:8000/sensor_data`
2. Move devices closer together
3. Check for Bluetooth interference
4. Verify nRF52 is in connectable mode (alert state)

### Issue: Buzzer doesn't stop after ACK
**Solutions**:
1. Check central logs for "✓ ACK sent" message
2. Verify ACK characteristic UUID matches in firmware and central
3. Try button hold (1 second) as fallback
4. Check firmware is in alert state (not already stopped)

### Issue: Health endpoint shows "degraded"
**Solutions**:
1. Check specific metrics in response
2. If `scanner.success_rate` low: Bluetooth adapter issue, try reset
3. If `ack.success_rate` low: RSSI/range issue, move devices closer
4. Check logs for specific errors

---

## Deployment Checklist

### Pre-Deployment
- [ ] All tests pass (Tests 1-8)
- [ ] 24-hour stability test completed
- [ ] Firmware version documented (commit hash)
- [ ] Device MAC addresses registered
- [ ] Network configuration verified
- [ ] E-ink display tested (if used)

### Deployment Day
- [ ] Power cycle RBPi (fresh start)
- [ ] Start central service: `sudo systemctl start leakseek`
- [ ] Verify service running: `sudo systemctl status leakseek`
- [ ] Check health endpoint: `curl http://localhost:8000/health`
- [ ] Trigger one test alert per device
- [ ] Verify web interface accessible
- [ ] Check e-ink display updating

### Post-Deployment Monitoring
- [ ] Monitor health endpoint every hour
- [ ] Check logs daily: `sudo journalctl -u leakseek -n 100`
- [ ] Track ACK success rate
- [ ] Monitor battery levels
- [ ] Document any issues or anomalies

---

## Quick Reference Commands

```bash
# Check Bluetooth status
sudo hciconfig hci0

# Reset Bluetooth
sudo hciconfig hci0 reset

# View central logs
sudo journalctl -u leakseek -f

# Check health
curl http://localhost:8000/health | jq

# Get sensor data
curl http://localhost:8000/sensor_data | jq

# Get discovered devices
curl http://localhost:8000/discovered_devices | jq

# Start central manually
cd server && python central_v2.py

# Test e-ink display
cd server && python test_eink.py
```

---

## Success Metrics

### Performance Targets
- **Alert detection latency**: < 2 seconds
- **ACK completion time**: < 5 seconds
- **Scanner uptime**: > 99.9%
- **ACK success rate**: > 95%
- **False positive rate**: < 0.1%

### System Health
- Health endpoint returns "healthy"
- Scanner consecutive failures: 0
- No Bluetooth adapter resets in 24 hours
- CPU usage: < 10%
- Memory usage: < 200MB

---

## Next Steps After Testing

Once all tests pass:
1. Document any configuration tuning done
2. Create backup of working configuration
3. Set up systemd service for auto-start
4. Configure monitoring/alerting (if needed)
5. Create runbook for common issues
6. Schedule regular maintenance checks
