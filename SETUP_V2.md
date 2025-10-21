# LeakSeek v2 Setup Guide

## Architecture Changes

**v1 (old)**: Connected to all sensors continuously via GATT
- Problem: BlueZ can't handle many simultaneous connections

**v2 (new)**: Advertisement-based monitoring
- Sensors advertise status in manufacturer data (non-connectable when OK)
- On leak: sensor switches to connectable + fast advertising
- Central only connects to send ACK
- Scalable to many sensors

---

## Hardware Requirements

- **nRF52 board** (Adafruit Feather nRF52840 or similar)
- **Raspberry Pi Zero 2 W** with built-in Bluetooth
- **Waveshare 2.13" e-Paper HAT** (optional but recommended)
- MicroSD card (8GB+)
- Power supplies

---

## Part 1: Firmware Setup (nRF52)

### 1. Install Arduino IDE and Board Support

```bash
# Install board package via Arduino IDE:
# File → Preferences → Additional Board Manager URLs:
https://adafruit.github.io/arduino-board-index/package_adafruit_index.json

# Tools → Board → Boards Manager
# Search for "nRF52" and install "Adafruit nRF52"
```

### 2. Upload Firmware

1. Open `leakseek_firmware/leakseek_firmware_v2.ino` in Arduino IDE
2. Select your board: Tools → Board → Adafruit nRF52 → [Your Board]
3. Select port: Tools → Port → [Your Device]
4. Upload (Ctrl/Cmd + U)

### 3. Verify Operation

Open Serial Monitor (115200 baud). You should see:
```
LeakSeek v2 - Advertisement-based monitoring
---------------------------------------------
Setting NORMAL mode (non-connectable)
Advertising started
Waiting for events...
```

### 4. Test with nRF Connect App

1. Install "nRF Connect" on your phone
2. Scan for devices
3. You should see "LeakSeek" advertising
4. Look at "Raw Data" → Manufacturer Data should show: `8B 01 01 00 00 64`
   - `8B 01` = Manufacturer ID (Konica Minolta)
   - `01` = Protocol version
   - `00` = Flags (no leak)
   - `00` = Sequence number
   - `64` = Battery (100%)

5. Wait for simulated leak (firmware triggers randomly at 5% chance)
6. When leak occurs, device becomes connectable
7. Connect and find the ACK characteristic: `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
8. Write the sequence number (e.g., `01`) to ACK
9. Device should return to non-connectable mode

---

## Part 2: Raspberry Pi Setup

### 1. Base System Setup

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Install Python 3 and dependencies
sudo apt install -y python3 python3-pip python3-venv python3-full
sudo apt install -y bluetooth bluez libbluetooth-dev
sudo apt install -y git spi-bcm2835 fonts-dejavu-core

# Enable SPI for e-ink (if using)
sudo raspi-config
# Interface Options → SPI → Enable
```

### 2. Clone Waveshare E-Paper Library (Optional)

```bash
cd ~/LeakSeek_NRF52_RBPi
git clone https://github.com/waveshare/e-Paper.git
```

**Important:** You must manually patch the Waveshare library to fix a systemd timing issue:

```bash
# Backup original
cp ~/LeakSeek_NRF52_RBPi/e-Paper/RaspberryPi_JetsonNano/python/lib/waveshare_epd/epdconfig.py ~/LeakSeek_NRF52_RBPi/e-Paper/RaspberryPi_JetsonNano/python/lib/waveshare_epd/epdconfig.py.backup

# Edit the file
nano ~/LeakSeek_NRF52_RBPi/e-Paper/RaspberryPi_JetsonNano/python/lib/waveshare_epd/epdconfig.py
```

Find the section around line 310-317 that looks like:
```python
if os.path.exists('/sys/bus/platform/drivers/gpiomem-bcm2835'):
    implementation = RaspberryPi()
elif os.path.exists('/sys/bus/platform/drivers/gpio-x3'):
    implementation = SunriseX3()
else:
    implementation = JetsonNano()
```

Replace the entire block with:
```python
# Force RaspberryPi (systemd timing workaround)
implementation = RaspberryPi()
```

**Why this is needed:** When systemd starts services early in boot, the GPIO driver isn't loaded yet. The detection falls through to JetsonNano as default, which fails to import. This manual override forces correct RaspberryPi detection.

### 3. Install Python Dependencies

```bash
cd ~/LeakSeek_NRF52_RBPi
python3 -m venv venv
source venv/bin/activate
pip install -r server/requirements.txt
```

### 4. Test the Server

```bash
cd ~/LeakSeek_NRF52_RBPi
source venv/bin/activate
cd server
uvicorn central_v2:app --host 0.0.0.0 --port 8000
```

You should see:
```
✓ E-ink display initialized  # (if HAT connected)
INFO:     Uvicorn running on http://0.0.0.0:8000
```

Open browser to `http://[raspberry-pi-ip]:8000`

### 5. Test Scanning

The server should automatically discover your LeakSeek device. Check the terminal:
```
Discovered new device: LeakSeek, 2E:5F:95:CD:03:7F
```

---

## Part 3: Captive Portal Setup

### 1. Install Access Point Software

```bash
sudo apt install -y hostapd dnsmasq
sudo systemctl stop hostapd
sudo systemctl stop dnsmasq
```

### 2. Configure Static IP

```bash
sudo nano /etc/dhcpcd.conf
```

Add at the end:
```
interface wlan0
static ip_address=10.0.0.1/24
nohook wpa_supplicant
```

### 3. Configure DHCP Server

```bash
sudo mv /etc/dnsmasq.conf /etc/dnsmasq.conf.orig
sudo nano /etc/dnsmasq.conf
```

Add:
```
interface=wlan0
dhcp-range=10.0.0.2,10.0.0.20,255.255.255.0,24h
address=/#/10.0.0.1
```

### 4. Configure Access Point

```bash
sudo nano /etc/hostapd/hostapd.conf
```

Add:
```
interface=wlan0
driver=nl80211
ssid=LeakSeek
hw_mode=g
channel=6
wmm_enabled=0
macaddr_acl=0
auth_algs=1
ignore_broadcast_ssid=0
wpa=2
wpa_passphrase=leakseek123
wpa_key_mgmt=WPA-PSK
wpa_pairwise=TKIP
rsn_pairwise=CCMP
```

```bash
sudo nano /etc/default/hostapd
```

Uncomment and set:
```
DAEMON_CONF="/etc/hostapd/hostapd.conf"
```

### 5. Enable Services

```bash
sudo systemctl unmask hostapd
sudo systemctl enable hostapd
sudo systemctl enable dnsmasq
```

---

## Part 4: Auto-Start Service

### 1. Create Systemd Service

```bash
sudo nano /etc/systemd/system/leakseek.service
```

Add:
```ini
[Unit]
Description=LeakSeek BLE Central Server
After=network.target bluetooth.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/LeakSeek_NRF52_RBPi/server
Environment="PATH=/home/pi/LeakSeek_NRF52_RBPi/venv/bin"
ExecStart=/home/pi/LeakSeek_NRF52_RBPi/venv/bin/uvicorn central_v2:app --host 0.0.0.0 --port 8000
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

### 2. Enable and Start

```bash
sudo systemctl daemon-reload
sudo systemctl enable leakseek
sudo systemctl start leakseek
```

### 3. Check Status

```bash
sudo systemctl status leakseek
sudo journalctl -u leakseek -f  # Follow logs
```

---

## Part 5: Testing & Validation

### Test Checklist

- [ ] Firmware uploads and runs (check Serial Monitor)
- [ ] nRF Connect shows manufacturer data
- [ ] Raspberry Pi scans and finds device
- [ ] Web UI accessible at `http://[pi-ip]:8000`
- [ ] Can register device via web UI
- [ ] E-ink display shows status (if connected)
- [ ] Simulated leak triggers alert
- [ ] ACK clears alert and returns to normal
- [ ] WiFi AP "LeakSeek" appears
- [ ] Phone auto-opens captive portal
- [ ] Manual ACK button works in web UI

### Debugging Tips

**No devices discovered:**
```bash
# Check Bluetooth is up
hciconfig
sudo hciconfig hci0 up

# Test scanning manually
sudo hcitool lescan
```

**E-ink not working:**
- Check SPI is enabled: `lsmod | grep spi`
- Verify pins: GPIO8(CE0), GPIO10(MOSI), GPIO11(SCLK), GPIO25(RST), GPIO17(DC), GPIO24(BUSY)
- Test with Waveshare examples

**Captive portal not opening:**
- Verify phone connected to WiFi
- Manually browse to `http://10.0.0.1`
- Check dnsmasq: `sudo systemctl status dnsmasq`

**ACK not clearing:**
- Verify firmware has ACK characteristic: `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- Check server logs for connection errors
- Use nRF Connect to manually test ACK write

---

## API Endpoints

- `GET /` - Web interface
- `GET /sensor_data` - All sensor data
- `GET /sensor_data/{address}` - Specific sensor
- `GET /discovered_devices` - Unregistered devices
- `GET /registered_devices` - Registered devices
- `POST /register/{address}?name=X` - Register device
- `POST /unregister/{address}` - Unregister device
- `POST /rename/{address}?new_name=X` - Rename device
- `POST /ack/{address}` - Manual ACK
- `GET /generate_204` - Android captive portal probe
- `GET /hotspot-detect.html` - iOS captive portal probe

---

## File Structure

```
LeakSeek_NRF52_RBPi/
├── leakseek_firmware/
│   ├── config.h                    # UUIDs, constants
│   ├── leakseek_firmware.ino       # Original (v1)
│   └── leakseek_firmware_v2.ino    # New (v2) ← USE THIS
├── server/
│   ├── central.py                  # Original (v1)
│   ├── central_v2.py               # New (v2) ← USE THIS
│   ├── utils.py                    # Device storage
│   ├── eink_display.py             # E-ink driver
│   ├── requirements.txt            # Python deps
│   └── sensor_data.json            # Persistent storage
├── web/
│   ├── index.html                  # Web UI
│   ├── app.js                      # Main app logic
│   ├── api.js                      # API client
│   ├── keyboard.js                 # On-screen keyboard
│   └── style.css                   # Styling
├── e-Paper/                        # Waveshare library (clone separately)
└── SETUP_V2.md                     # This file
```

---

## Protocol Specification

### Manufacturer Data Format

Offset | Size | Field | Description
-------|------|-------|------------
0 | 1 | Protocol | Always `0x01`
1 | 1 | Flags | Bit0=leak, Bit1=needs_ack
2 | 1 | Sequence | Increments on each event
3 | 1 | Battery | Percentage (0-100)

### Advertising Modes

**Normal Mode** (no leak):
- Type: Non-connectable
- Interval: 1000ms
- Flags: `0x00`

**Alert Mode** (leak detected):
- Type: Connectable
- Interval: 20-30ms (first 5s), then 100ms
- Flags: `0x03` (leak + needs_ack)

### ACK Flow

1. Sensor detects leak
2. Sets `flags = 0x03`, increments `seq`
3. Switches to connectable, fast advertising
4. Central connects, reads `seq` from manufacturer data
5. Central writes `seq` to ACK characteristic (`6e400002-...`)
6. Sensor clears flags, returns to normal mode

---

## Troubleshooting

### Issue: "externally-managed-environment" error
**Solution**: Always use venv (already in instructions)

### Issue: Bluetooth permission denied
**Solution**: 
```bash
sudo usermod -a -G bluetooth pi
# Logout and login
```

### Issue: E-ink shows "ImportError"
**Solution**: Library path may be wrong. Edit `eink_display.py` line 12-16 to point to your Waveshare lib location.

### Issue: High latency on ACK
**Solution**: 
- Reduce scan interval in firmware
- Check WiFi/BLE interference (use channel 1 or 11 for AP)
- Disable power saving: `sudo iwconfig wlan0 power off`

---

## Demo Day Checklist

**Hardware:**
- [ ] nRF52 powered and running
- [ ] RPi powered with e-ink connected
- [ ] Backup power supplies

**Software:**
- [ ] LeakSeek service running: `sudo systemctl status leakseek`
- [ ] WiFi AP active: `iwconfig wlan0`
- [ ] At least one sensor pre-registered

**Demo Script:**
1. Show e-ink display: "All Clear"
2. Connect phone to "LeakSeek" WiFi
3. Show web interface with sensor list
4. Trigger leak (wait for 5% random, or modify firmware to trigger on button press)
5. Watch e-ink switch to ALERT mode
6. Web UI shows red alert
7. Click sensor → shows details
8. ACK clears automatically
9. E-ink returns to "All Clear"

**Backup Plans:**
- If e-ink fails → disable and show web only
- If auto-ACK fails → use Manual ACK button
- If captive portal doesn't open → show direct IP

---

## Future Enhancements

- [ ] Add hardware button to nRF for manual leak trigger
- [ ] Battery monitoring with low-battery alerts
- [ ] Historical event log with timestamps
- [ ] Multiple sensor types (temperature, humidity)
- [ ] SMS/email notifications
- [ ] Directed advertising for faster discovery
- [ ] Over-the-air firmware updates
- [ ] Pagination/scrolling for many sensors on e-ink

---

## Contact & Support

Project: LeakSeek - Water Leak Detection System
Version: 2.0
Last Updated: 2025-10-20
