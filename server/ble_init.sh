#!/bin/bash
# BLE Initialization Script for LeakSeek
# Ensures Bluetooth adapter is powered on before starting the server

echo "Initializing Bluetooth adapter..."

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
    exit 0
else
    echo "✗ Failed to power on Bluetooth adapter"
    echo "Trying one more time..."
    sudo systemctl restart bluetooth
    sleep 2
    sudo rfkill unblock bluetooth
    sudo hciconfig hci0 up
    echo -e "power on\nquit" | sudo bluetoothctl > /dev/null 2>&1
    sleep 1
    
    if bluetoothctl show | grep -q "Powered: yes"; then
        echo "✓ Bluetooth adapter is powered on (after retry)"
        exit 0
    else
        echo "✗ Still failed. Check bluetooth service status."
        exit 1
    fi
fi

