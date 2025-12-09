'''
Utilities of the central.py file
Focused on local storage and device management
Currently we are focused on basic filestorage with a JSON file on disk.
'''

import json as JSON
import os
import logging

# Use absolute path for data file to avoid CWD issues
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_FILE = os.path.join(BASE_DIR, "sensor_data.json")

def load_data():
    if os.path.exists(DATA_FILE):
        try:
            with open(DATA_FILE, 'r') as f:
                return JSON.load(f)
        except Exception as e:
            logging.error(f"Failed to load data from {DATA_FILE}: {e}")
            return {}
    return {}

def save_data(data):
    try:
        # Atomic write: write to temp file then rename
        tmp_file = DATA_FILE + ".tmp"
        with open(tmp_file, 'w') as f:
            JSON.dump(data, f, indent=4)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp_file, DATA_FILE)
    except Exception as e:
        logging.error(f"Failed to save data to {DATA_FILE}: {e}")

def clear_data():
    if os.path.exists(DATA_FILE):
        try:
            os.remove(DATA_FILE)
        except Exception as e:
            logging.error(f"Failed to clear data file: {e}")
    return {}

def add_device(address, name):
    try:
        devices = load_data()
        devices[address] = {"name": name, "address": address}
        save_data(devices)
    except Exception as e:
        logging.error(f"Error adding device {address}: {e}")

def remove_device(address):
    try:
        devices = load_data()
        if address in devices:
            del devices[address]
            save_data(devices)
    except Exception as e:
        logging.error(f"Error removing device {address}: {e}")

def device_exists(address):
    devices = load_data()
    return address in devices

def rename_device(address, new_name):
    try:
        devices = load_data()
        if address in devices:
            devices[address]["name"] = new_name
            save_data(devices)
    except Exception as e:
        logging.error(f"Error renaming device {address}: {e}")
