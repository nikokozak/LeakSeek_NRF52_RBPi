#!/bin/bash
# WiFi AP Initialization Script for LeakSeek
# Ensures wlan0 is properly configured as access point

echo "Initializing WiFi Access Point..."

# Wait for wlan0 to be available
for i in {1..10}; do
    if ip link show wlan0 &> /dev/null; then
        echo "✓ wlan0 interface found"
        break
    fi
    echo "Waiting for wlan0... ($i/10)"
    sleep 1
done

# Bring wlan0 down and back up to reset
sudo ip link set wlan0 down
sleep 1
sudo ip link set wlan0 up
sleep 1

# Ensure static IP is set
if ! ip addr show wlan0 | grep -q "192.168.4.1"; then
    echo "Setting static IP on wlan0..."
    sudo ip addr flush dev wlan0
    sudo ip addr add 192.168.4.1/24 dev wlan0
fi

# Verify IP is set
if ip addr show wlan0 | grep -q "192.168.4.1"; then
    echo "✓ wlan0 has IP 192.168.4.1/24"
else
    echo "✗ Failed to set IP on wlan0"
    exit 1
fi

echo "✓ WiFi AP initialization complete"

