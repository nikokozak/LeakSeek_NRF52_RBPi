# LeakSeek Alert Flow - Correct Behavior

## Overview
The buzzer is a **physical safety alert** that guides the user to the leak location on the boat. Remote ACK from the central monitoring system does NOT silence the buzzer - only the person physically at the leak can silence it by pressing the button.

---

## State Machine

```
┌──────────────┐
│   NORMAL     │  Monitoring for water
│              │  • Buzzer: OFF
│              │  • Advertising: 1000ms
│              │  • Flags: 0x00 (no leak, no ACK needed)
└──────┬───────┘
       │
       │ Water detected!
       │
       ↓
┌──────────────┐
│   ALERT      │  Leak detected, buzzing, requesting ACK
│  (needs_ack) │  • Buzzer: BEEPING (3-beep pattern)
│              │  • Advertising: 20ms → 100ms (fast)
│              │  • Flags: 0x03 (leak=1, needs_ack=1)
│              │  • Connectable: YES
└──────┬───────┘
       │
       │ Central connects & sends ACK
       │
       ↓
┌──────────────┐
│   ALERT      │  Central acknowledged, but user must locate leak
│ (ACKed)      │  • Buzzer: STILL BEEPING! ← Key point!
│              │  • Advertising: 1000ms (normal speed)
│              │  • Flags: 0x01 (leak=1, needs_ack=0)
│              │  • Connectable: NO (back to normal mode)
└──────┬───────┘
       │
       │ User presses button (1 sec hold)
       │
       ↓
┌──────────────┐
│   STOPPED    │  User found leak and silenced alarm
│              │  • Buzzer: OFF
│              │  • Advertising: 1000ms
│              │  • Flags: 0x01 (leak=1, needs_ack=0)
│              │  • Sensor: FROZEN (stops reading)
└──────┬───────┘
       │
       │ Power cycle / reboot
       │
       ↓
┌──────────────┐
│   NORMAL     │  Back to monitoring
└──────────────┘
```

---

## Detailed Flow

### Step 1: Water Detected
**Trigger**: RC timing drops below threshold (< 20ms)

**Firmware Actions**:
- Increment sequence number
- Set `current_flags = 0x03` (leak=1, needs_ack=1)
- Enter STATE_ALERT
- Start buzzer (3-beep pattern)
- Switch to fast advertising (20ms for 5s, then 100ms)
- Become connectable

**User Experience**:
- 🔊 Buzzer starts beeping (loud, continuous)
- 💡 LED blinks rapidly
- "Where's the leak?!"

**Central Response**:
- Detects advertisement with needs_ack=1
- Logs: "⚠️ ALERT from XX:XX:XX:XX:XX:XX"
- Queues ACK task
- Updates web interface
- Updates e-ink display

---

### Step 2: Central Sends ACK
**Trigger**: Central connects via BLE and writes to ACK characteristic

**Firmware Actions**:
- Log: "ACK received via BLE - central acknowledged, but buzzer continues"
- Set `current_flags = 0x01` (leak=1, needs_ack=0)
- **Stay in STATE_ALERT** (don't transition to STOPPED!)
- Return to normal advertising (1000ms)
- Return to non-connectable mode
- **Keep buzzer running** ← Critical!

**User Experience**:
- 🔊 Buzzer continues beeping (unchanged)
- 💡 LED still blinking rapidly
- User still searching for leak...

**Central Response**:
- Logs: "✓ ACK sent to XX:XX:XX:XX:XX:XX"
- Updates sensor data: `needs_ack = false`
- Web interface shows "ACKed" status
- **Does NOT affect buzzer** (that's local to the module)

**Why?**
- The central ACK just means "we know about the alert"
- It updates the monitoring dashboard
- But the **physical alert continues** until the user locates the leak
- The buzzer is the user's guide - it should not stop remotely!

---

### Step 3: User Locates Leak
**User Actions**:
- Follows buzzer sound to the leak location
- Visually confirms water/leak
- Holds button for 1 second

**Firmware Actions**:
- Log: "BUTTON HELD FOR 1 SECOND - ACKNOWLEDGING ALERT"
- Transition to STATE_STOPPED
- **Stop buzzer** (finally!)
- Stop LED blinking (or slower blink)
- Freeze sensor readings (no more checks)
- Advertising continues with flags 0x01 (leak=1, needs_ack=0)

**User Experience**:
- 🔇 Blessed silence!
- ✅ "I found it, I can deal with it now"
- Sensor frozen (won't re-trigger)

**Central Response**:
- Continues monitoring
- Sensor data shows leak=1, needs_ack=0
- Status: "Leak present, user acknowledged"

---

### Step 4: Reset System
**User Actions**:
- Dry the sensor
- Power cycle the nRF52 module (turn off and on)

**Firmware Actions**:
- Complete reset
- Return to STATE_NORMAL
- Resume monitoring for water

**User Experience**:
- System ready to detect leaks again
- Back to normal operation

---

## Key Behavior Points

### ✅ Correct Behavior
1. **Central ACK** = "Monitoring system knows about the leak"
   - Updates dashboard
   - Clears needs_ack flag
   - Returns advertising to normal speed
   - **Buzzer continues**

2. **Button Press** = "User physically found the leak and wants silence"
   - Stops buzzer
   - Freezes sensor
   - Requires reboot to resume

3. **Power Cycle** = "Leak is fixed, resume normal operation"
   - Complete reset
   - Back to monitoring mode

### ❌ Incorrect Behavior (Previous)
1. Central ACK → Buzzer stops
   - Problem: User never physically locates leak
   - Problem: Remote system can silence safety alarm

---

## Use Case Scenarios

### Scenario 1: Normal Leak Detection
1. Small leak develops in bilge
2. Water contacts sensor → Buzzer starts
3. Central receives alert → Dashboard updated
4. Owner hears buzzer → Follows sound to bilge
5. Owner finds leak → Presses button → Silence
6. Owner fixes leak → Dries sensor → Power cycles
7. System resumes monitoring

**Result**: ✅ Leak found and addressed

---

### Scenario 2: False Alarm (e.g., splash)
1. Wave splashes sensor → Buzzer starts
2. Central receives alert → Dashboard updated
3. Owner hears buzzer → Goes to check
4. Owner sees no actual leak → Presses button → Silence
5. Sensor dries naturally
6. Owner power cycles when convenient
7. System resumes monitoring

**Result**: ✅ False alarm silenced, no real issue

---

### Scenario 3: Remote Monitoring
1. Leak occurs while owner not on board
2. Sensor detects → Buzzer starts (no one hears it)
3. Central detects → Alert sent to cloud/phone
4. Owner receives phone notification
5. Owner arrives at boat → Follows buzzer to leak
6. Owner finds leak → Presses button → Silence
7. Owner fixes leak → Power cycles sensor

**Result**: ✅ Remote alert notifies owner, buzzer guides them on arrival

---

### Scenario 4: Multiple Sensors
1. Leak in bilge (Sensor A)
2. Leak in head (Sensor B)
3. Both sensors buzzing
4. Central ACKs both → Dashboard updated
5. **Both buzzers continue** (both need physical location)
6. Owner finds bilge leak → Presses Sensor A button
7. Owner finds head leak → Presses Sensor B button
8. Both silent
9. Owner fixes both → Power cycles both

**Result**: ✅ User physically located and addressed both leaks

---

## Manufacturer Data Protocol

### Normal State (0x00)
```
Flags: 0x00
Bit 0 (leak): 0
Bit 1 (needs_ack): 0
```
- No leak detected
- No action needed
- Advertising: 1000ms, non-connectable

### Alert Requesting ACK (0x03)
```
Flags: 0x03
Bit 0 (leak): 1
Bit 1 (needs_ack): 1
```
- Leak detected
- Requesting central ACK
- Advertising: 20ms→100ms, connectable
- **Buzzer: BEEPING**

### Alert After ACK (0x01)
```
Flags: 0x01
Bit 0 (leak): 1
Bit 1 (needs_ack): 0
```
- Leak still present
- Central acknowledged
- Advertising: 1000ms, non-connectable
- **Buzzer: STILL BEEPING** ← Key difference!

### Stopped (0x01)
```
Flags: 0x01
Bit 0 (leak): 1
Bit 1 (needs_ack): 0
State: STOPPED (internal)
```
- Leak present
- User acknowledged (button pressed)
- Advertising: 1000ms, non-connectable
- **Buzzer: OFF** (finally!)
- Sensor: Frozen

---

## FAQ

### Q: Why doesn't central ACK stop the buzzer?
**A**: Safety. The buzzer is a physical guide to the leak location. Only the person who physically finds the leak should be able to silence it. Remote silence could mask a real problem.

### Q: What if the buzzer is annoying and there's no leak?
**A**: Press the button. If it's a false alarm (splash, condensation), the button press silences it immediately. Then dry the sensor and power cycle when convenient.

### Q: What if I want remote silence capability?
**A**: This is a design decision for safety. However, you could add a "remote silence" command that simulates a button press via BLE. But this should be carefully considered for safety-critical marine applications.

### Q: Can I change this behavior?
**A**: Yes, modify `ack_write_callback()` to call `set_system_state(STATE_STOPPED)` instead of just clearing the needs_ack flag. But this defeats the purpose of the physical alert.

### Q: What if the button doesn't work?
**A**: Power cycle the module to silence the buzzer (it will return to NORMAL state). If leak is still present, buzzer will start again. Fix: Replace/check button wiring.

---

## Summary

**The buzzer is your friend!** It's designed to guide you to the leak, not annoy you. When it beeps:

1. 🔊 Follow the sound
2. 👀 Find the leak
3. 👆 Press the button
4. 🔧 Fix the leak
5. 🔄 Power cycle
6. ✅ Done!

The central monitoring system helps you track multiple sensors and get remote alerts, but the **physical buzzer is the primary safety mechanism** for on-board leak location.
