'''
Utilities of the central.py file
Focused on local storage and device management
Currently we are focused on basic filestorage with a JSON file on disk.
'''

import json as JSON
import os

DATA_FILE = "sensor_data.json"

def load_data():
    if os.path.exists(DATA_FILE):
        with open(DATA_FILE, 'r') as f:
            return JSON.load(f)
    return {}

def save_data(data):
    with open(DATA_FILE, 'w') as f:
        JSON.dump(data, f, indent=4)

def clear_data():
    if os.path.exists(DATA_FILE):
        os.remove(DATA_FILE)
    return {}

def add_device(address, name):
    devices = load_data()
    devices[address] = {"name": name, "address": address}
    save_data(devices)

def remove_device(address):
    devices = load_data()
    if address in devices:
        del devices[address]
        save_data(devices)

def device_exists(address):
    devices = load_data()
    return address in devices
