# LeakSeek Raspberry Pi Zero W Setup Guide

This guide covers setting up the LeakSeek server on a Raspberry Pi Zero W 1.1, including:
- Server installation
- Systemd service configuration
- Captive portal setup for direct WiFi access

## Prerequisites

- Raspberry Pi Zero W 1.1 with Raspberry Pi OS (Lite recommended)
- Python 3.9 or newer
- Bluetooth enabled and working
- Root/sudo access

## 1. Initial Setup

### Install System Dependencies

```bash
sudo apt-get update
sudo apt-get install -y python3-pip python3-venv bluetooth bluez
```

### Enable Bluetooth

```bash
sudo systemctl enable bluetooth
sudo systemctl start bluetooth
```

### Clone/Copy Project

```bash
cd /home/niko
# Copy your LeakSeek project to this location
# Should have: LeakSeek_NRF52_RBPi/server/ and LeakSeek_NRF52_RBPi/web/
```

## 2. Python Environment Setup

### Create Virtual Environment

```bash
cd /home/niko/LeakSeek_NRF52_RBPi
python3 -m venv venv
source venv/bin/activate
```

### Install Python Dependencies

```bash
cd /home/niko/LeakSeek_NRF52_RBPi
source venv/bin/activate
pip3 install --upgrade pip
pip3 install -r server/requirements.txt
```

## 3. Configure Bluetooth

Ensure Bluetooth starts automatically and stays powered on:

```bash
sudo nano /etc/bluetooth/main.conf
```

Add/uncomment in the `[Policy]` section:
```
[Policy]
AutoEnable=true
```

Restart Bluetooth:
```bash
sudo systemctl restart bluetooth
```

Make the BLE init script executable:
```bash
cd /home/niko/LeakSeek_NRF52_RBPi/server
chmod +x ble_init.sh
```

## 4. Test the Server

Before setting up as a service, test that everything works:

```bash
cd /home/niko/LeakSeek_NRF52_RBPi
source venv/bin/activate
cd server

# Initialize Bluetooth adapter
sudo ./ble_init.sh

# Run the server
python3 central.py
```

You should see:
- "BLE background thread started"
- Flask server running on port 2300

Test from another device on the same network by visiting: `http://<pi-ip-address>:2300`

Press Ctrl+C to stop.

## 5. Create Systemd Service

This makes the server start automatically on boot.

### Create Service File

```bash
sudo nano /etc/systemd/system/leakseek.service
```

Paste the following content:

```ini
[Unit]
Description=LeakSeek BLE Server
After=bluetooth.service network.target
Requires=bluetooth.service

[Service]
Type=simple
User=niko
WorkingDirectory=/home/niko/LeakSeek_NRF52_RBPi/server
Environment="PATH=/home/niko/LeakSeek_NRF52_RBPi/venv/bin"
# Initialize Bluetooth adapter before starting
ExecStartPre=/home/niko/LeakSeek_NRF52_RBPi/server/ble_init.sh
ExecStart=/home/niko/LeakSeek_NRF52_RBPi/venv/bin/python3 /home/niko/LeakSeek_NRF52_RBPi/server/central.py
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

### Enable and Start Service

```bash
# Reload systemd to recognize new service
sudo systemctl daemon-reload

# Enable service to start on boot
sudo systemctl enable leakseek.service

# Start service now
sudo systemctl start leakseek.service

# Check status
sudo systemctl status leakseek.service
```

### Useful Service Commands

```bash
# View live logs
sudo journalctl -u leakseek.service -f

# Restart service
sudo systemctl restart leakseek.service

# Stop service
sudo systemctl stop leakseek.service

# Disable auto-start
sudo systemctl disable leakseek.service
```

## 6. Captive Portal Setup

This allows users to connect directly to the Pi's WiFi and access the LeakSeek interface without internet.

### Install dnsmasq and hostapd

```bash
sudo apt-get install -y dnsmasq hostapd
sudo systemctl stop dnsmasq
sudo systemctl stop hostapd
```

### Configure Static IP for wlan0

Edit dhcpcd configuration:

```bash
sudo nano /etc/dhcpcd.conf
```

Add at the end:

```
interface wlan0
    static ip_address=192.168.4.1/24
    nohook wpa_supplicant
```

### Configure hostapd (WiFi Access Point)

Create hostapd config:

```bash
sudo nano /etc/hostapd/hostapd.conf
```

Add:

```
interface=wlan0
driver=nl80211
ssid=LeakSeek
hw_mode=g
channel=7
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

**Note:** Change `ssid` and `wpa_passphrase` to your preferred network name and password.

Tell hostapd where the config is:

```bash
sudo nano /etc/default/hostapd
```

Find `#DAEMON_CONF=""` and replace with:

```
DAEMON_CONF="/etc/hostapd/hostapd.conf"
```

### Configure dnsmasq (DHCP and DNS)

Backup original config:

```bash
sudo mv /etc/dnsmasq.conf /etc/dnsmasq.conf.orig
```

Create new config:

```bash
sudo nano /etc/dnsmasq.conf
```

Add:

```
interface=wlan0
dhcp-range=192.168.4.2,192.168.4.20,255.255.255.0,24h
address=/#/192.168.4.1
```

This configuration:
- Serves DHCP on wlan0
- Assigns IPs from 192.168.4.2 to 192.168.4.20
- Routes ALL DNS queries to 192.168.4.1 (captive portal behavior)

### Update Flask to Run on Port 80

Edit `central.py` and change the last line from:

```python
app.run(host='0.0.0.0', port=5000, debug=True)
```

To:

```python
app.run(host='0.0.0.0', port=80, debug=False)
```

**Important:** Running on port 80 requires root privileges. Update the systemd service:

```bash
sudo nano /etc/systemd/system/leakseek.service
```

Change the `User=` line to:

```ini
User=root
```

Alternatively, use port 80 redirect via iptables (keeps service as user `niko`):

```bash
sudo iptables -t nat -A PREROUTING -p tcp --dport 80 -j REDIRECT --to-port 2300
```

To make iptables rule persistent:

```bash
sudo apt-get install -y iptables-persistent
sudo netfilter-persistent save
```

### Enable Services

```bash
sudo systemctl unmask hostapd
sudo systemctl enable hostapd
sudo systemctl enable dnsmasq
```

### Reboot

```bash
sudo reboot
```

After reboot:
1. Look for WiFi network "LeakSeek" (or your custom SSID)
2. Connect with password "leakseek123" (or your custom password)
3. Browser should automatically open captive portal
4. If not, manually visit: `http://192.168.4.1` or `http://leakseek.local`

## 7. Troubleshooting

### Service Won't Start

```bash
# Check logs
sudo journalctl -u leakseek.service -n 50

# Common issues:
# - Bluetooth not ready: ensure bluetooth.service is running
# - Permission denied on port 80: ensure service runs as root or use iptables
# - Module not found: ensure venv path is correct in service file
```

### Bluetooth Issues

**Adapter not powered:**
```bash
# Run the BLE init script
cd /home/niko/LeakSeek_NRF52_RBPi/server
sudo ./ble_init.sh

# Or manually:
sudo rfkill unblock bluetooth
sudo hciconfig hci0 up
echo -e "power on\nquit" | sudo bluetoothctl

# Verify
bluetoothctl show
```

**BlueZ "Operation already in progress" error:**

This is a race condition where scans start too quickly. The code now includes delays and error handling, but if you still see it:

```bash
# Restart Bluetooth to clear state
sudo systemctl restart bluetooth
sleep 2
sudo ./ble_init.sh

# Restart the service
sudo systemctl restart leakseek.service
```

**Other Bluetooth issues:**
```bash
# Check Bluetooth status
sudo systemctl status bluetooth

# Restart Bluetooth
sudo systemctl restart bluetooth

# Check if hci0 device exists
hciconfig

# Reset Bluetooth adapter
sudo hciconfig hci0 down
sudo hciconfig hci0 up
```

### WiFi Access Point Not Visible

```bash
# Check hostapd status
sudo systemctl status hostapd

# Check hostapd logs
sudo journalctl -u hostapd -n 50

# Test hostapd config
sudo hostapd -d /etc/hostapd/hostapd.conf

# Check if wlan0 has correct IP
ip addr show wlan0
```

### Can't Access Web Interface

```bash
# Check if Flask is running
sudo netstat -tuln | grep 80

# Check firewall (should be off by default on Pi)
sudo iptables -L

# Test from Pi itself
curl http://localhost:80
```

## 8. Performance Optimization for Pi Zero W

The Pi Zero W has limited resources (512MB RAM, single-core 1GHz). Here are some tips:

### Reduce Logging

In production, minimize logging to reduce disk I/O:

```python
# In central.py, comment out print statements or use proper logging with WARNING level
```

### Use Raspberry Pi OS Lite

The Lite version has no desktop environment, saving significant RAM.

### Disable Unused Services

```bash
# Disable services you don't need
sudo systemctl disable avahi-daemon
sudo systemctl disable triggerhappy
```

### Monitor Resources

```bash
# Check memory usage
free -h

# Check CPU usage
top

# Check service resource usage
sudo systemctl status leakseek.service
```

## 9. Updating the Code

When you update the code:

```bash
cd /home/niko/LeakSeek_NRF52_RBPi/server
sudo systemctl stop leakseek.service

# Update your code via git pull, scp, or direct edit

sudo systemctl start leakseek.service
```

## 10. Security Notes

- The default setup has no authentication
- Captive portal is open to anyone in WiFi range
- For production, consider:
  - Adding basic auth to Flask routes
  - Using HTTPS with self-signed cert
  - Stronger WiFi password
  - MAC address filtering in hostapd

## Support

For issues specific to:
- **Bleak**: https://github.com/hbldh/bleak
- **Flask**: https://flask.palletsprojects.com/
- **Raspberry Pi**: https://www.raspberrypi.org/forums/

