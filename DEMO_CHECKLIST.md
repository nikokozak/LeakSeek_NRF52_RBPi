# LeakSeek Demo Day Quick Reference

## Pre-Demo Setup (30 min before)

### 1. Hardware Check
```bash
# Power on RPi and nRF52
# Wait 30 seconds for boot

# SSH into RPi
ssh pi@[raspberry-pi-ip]  # or use monitor/keyboard

# Check service is running
sudo systemctl status leakseek

# Should show: "active (running)" in green
```

### 2. Verify Everything Works
```bash
# Check logs
sudo journalctl -u leakseek -n 50

# Should see:
# ✓ E-ink display initialized
# Uvicorn running on http://0.0.0.0:8000
# Discovered device: LeakSeek, [address]
```

### 3. Pre-Register a Sensor
```bash
# On your laptop, open browser
http://[raspberry-pi-ip]:8000

# Click "Discover Sensors"
# Click "Add" next to LeakSeek device
# Name it "Demo Sensor"
# Confirm it appears on main screen
```

### 4. E-ink Display Check
- Should show: "LeakSeek" header
- Should show: "✓ All Clear"
- Should show: "1 sensor(s) active"
- Should list: "• Demo Sensor"

---

## Demo Script (5 minutes)

### Opening (30 sec)
> "LeakSeek is a water leak detection system using BLE sensors and a Raspberry Pi hub."

### Show the System (1 min)

**Point to nRF52:**
> "This is our sensor. It advertises its status every second using Bluetooth Low Energy."

**Point to E-ink display:**
> "The e-ink display shows all registered sensors. Right now: All Clear."

**Show phone:**
> "Let me connect to the LeakSeek WiFi network..."
[Connect phone to "LeakSeek" network, password: leakseek123]

> "...and it automatically opens the control panel."
[If it doesn't auto-open, browse to http://10.0.0.1]

### Demonstrate Registration (1 min)
> "Here I can see my registered sensor. I can click it for details..."

[Click sensor card]

> "...see its status, battery level, and last update time."

[Go back]

> "I can also discover new sensors..."

[Click menu → Discover Sensors]

> "...and register them with custom names."

### Trigger Alert (2 min)
> "Now, let me simulate a leak detection."

**Wait for firmware to randomly trigger (5% chance per second), OR:**

**Quick trigger method (if modified firmware):**
- Press button on nRF52 (if you added this)
- Wait ~10 seconds for next random cycle

**When alert triggers:**

**E-ink display changes:**
- Black banner: "⚠ 1 LEAK ALERT"
- Lists: "• Demo Sensor"

**Web UI changes:**
- Sensor card turns red
- Shows value "1"

> "The sensor detected water and switched to fast advertising mode."

[Watch terminal logs]

> "The Raspberry Pi connects and acknowledges the alert..."

**Terminal shows:**
```
⚠️  ALERT from [address], seq=1, queuing ACK
ACK attempt 1/2 for [address], seq=1
✓ ACK sent to [address], seq=1
```

**After ~2-5 seconds:**

> "...and the system clears once acknowledged."

**E-ink returns to:** "✓ All Clear"
**Web UI:** Card turns back to normal, value "0"

### Closing (30 sec)
> "This architecture scales well because we only connect when needed. The sensor can run for months on a coin cell, and the Raspberry Pi can monitor dozens of sensors."

---

## Quick Troubleshooting

### Problem: Service not running
```bash
sudo systemctl start leakseek
sudo journalctl -u leakseek -f  # Watch for errors
```

### Problem: No devices discovered
```bash
# Check Bluetooth
hciconfig
sudo hciconfig hci0 up

# Restart nRF52 (power cycle)
```

### Problem: E-ink frozen
```bash
# Restart service (will reinitialize display)
sudo systemctl restart leakseek
```

### Problem: WiFi AP not visible
```bash
# Check hostapd
sudo systemctl status hostapd

# Restart if needed
sudo systemctl restart hostapd
```

### Problem: Captive portal doesn't open
- Manually browse to: `http://10.0.0.1`
- Make sure phone WiFi assist is OFF
- Try airplane mode → WiFi only

### Problem: Alert not triggering
**Modify firmware for instant trigger:**
```cpp
// In loop(), change:
uint8_t sensor_state = random(0, 100) > 95 ? 1 : 0;

// To:
uint8_t sensor_state = 1;  // Always trigger
```
Re-upload firmware (2 min)

---

## Backup Demo (If BLE Fails)

### Plan B: Show Architecture
1. Open SETUP_V2.md
2. Explain protocol with manufacturer data diagram
3. Show code:
   - `leakseek_firmware_v2.ino` - advertising modes
   - `central_v2.py` - detection_callback parsing
   - `eink_display.py` - rendering logic

### Plan C: Video/Screenshots
Take these NOW as backup:
- [ ] E-ink showing "All Clear"
- [ ] E-ink showing "LEAK ALERT"
- [ ] Web UI with sensor list
- [ ] Web UI showing alert (red card)
- [ ] nRF Connect showing manufacturer data

---

## Key Talking Points

**Why advertisement-based?**
- BlueZ can't handle many simultaneous connections
- Advertising is broadcast: one sensor → many receivers
- Only connect when acknowledgment needed
- Battery efficient (advertising uses 10x less power than connection)

**Why e-ink?**
- Readable in bright light
- Ultra-low power (only uses power on update)
- Persistent display (shows status even if RPi loses power)
- Perfect for always-on monitoring

**Scalability:**
- Current: 1 sensor demo
- Tested: Up to 10 sensors
- Theoretical: 50+ sensors (limited by scan time, not connections)

**Real-world use:**
- Under sinks, washing machines, water heaters
- Basements, attics, crawl spaces
- Museums, archives (valuable documents)
- Server rooms (cooling systems)

---

## Command Reference

```bash
# Start/stop service
sudo systemctl start leakseek
sudo systemctl stop leakseek
sudo systemctl restart leakseek

# View logs
sudo journalctl -u leakseek -f
sudo journalctl -u leakseek -n 100

# Check network
iwconfig wlan0
ip addr show wlan0

# Check Bluetooth
hciconfig
hcitool lescan

# Reboot (if all else fails)
sudo reboot
```

---

## Post-Demo Notes

Things to mention if asked:

**Battery Life:**
- Advertising @ 1Hz: ~6 months on CR2032
- With sleep modes: ~1 year

**Range:**
- Bluetooth: ~10m indoor, ~30m outdoor
- Can add mesh networking for larger spaces

**Cost:**
- nRF52 sensor: ~$25
- Raspberry Pi Zero 2 W: ~$15
- E-ink HAT: ~$15
- Total: ~$55 per hub, ~$25 per sensor

**Next Steps:**
- Production PCB design
- Waterproof enclosure
- Battery holder + coin cell
- Conductive probe leads
- FCC/CE certification

---

## Emergency Contacts

nRF52 Serial Monitor: 115200 baud
RPi Default User: pi
Default Password: [your password]
WiFi SSID: LeakSeek
WiFi Password: leakseek123
Web Interface: http://10.0.0.1 or http://[pi-ip]:8000

**Arduino IDE Port Issues:**
- Tools → Port → Look for "Adafruit Feather"
- If not found: Press reset button twice (bootloader mode)

Good luck! 🚀
