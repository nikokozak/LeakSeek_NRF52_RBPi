#!/bin/bash
# BLE Initialization Script for LeakSeek
# Ensures Bluetooth adapter is powered on before starting the server

echo "Initializing Bluetooth adapter..."

# Stop bluetooth service and kill any stuck processes
sudo systemctl stop bluetooth
sudo pkill bluetoothd 2>/dev/null
sleep 2

# Start bluetooth service fresh
sudo systemctl start bluetooth
sleep 2

# Unblock Bluetooth (in case it's soft-blocked)
sudo rfkill unblock bluetooth
sleep 1

# Bring up the HCI interface
sudo hciconfig hci0 up
sleep 1

# Power on via bluetoothctl
echo -e "power on\nquit" | sudo bluetoothctl > /dev/null 2>&1
sleep 1

# Verify it's powered on
if bluetoothctl show | grep -q "Powered: yes"; then
    echo "✓ Bluetooth adapter is powered on"
    
    # Test D-Bus connectivity
    if timeout 3 dbus-send --system --print-reply --dest=org.bluez /org/bluez/hci0 org.freedesktop.DBus.Introspectable.Introspect > /dev/null 2>&1; then
        echo "✓ BlueZ D-Bus is responding"
        exit 0
    else
        echo "⚠ Bluetooth powered but D-Bus not responding, retrying..."
        sudo systemctl restart bluetooth
        sleep 2
        sudo rfkill unblock bluetooth
        sudo hciconfig hci0 up
        echo -e "power on\nquit" | sudo bluetoothctl > /dev/null 2>&1
        sleep 1
        echo "✓ Retry complete"
        exit 0
    fi
else
    echo "✗ Failed to power on Bluetooth adapter"
    exit 1
fi

