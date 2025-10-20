#!/bin/bash
# Ensure Bluetooth is unblocked and enabled
# Run this once, or add to systemd service

sudo rfkill unblock bluetooth
sudo hciconfig hci0 up
