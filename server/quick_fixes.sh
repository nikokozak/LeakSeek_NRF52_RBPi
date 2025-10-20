#!/bin/bash
# Quick fixes for Bluetooth issues on RPi

echo "Checking Bluetooth status..."
hciconfig

echo ""
echo "Attempting to bring up Bluetooth adapter..."
sudo hciconfig hci0 up

echo ""
echo "Restarting Bluetooth service..."
sudo systemctl restart bluetooth

echo ""
echo "Checking status again..."
hciconfig

echo ""
echo "Try running the server now!"
