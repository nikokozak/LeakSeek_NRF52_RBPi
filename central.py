import asyncio
from bleak import BleakScanner, BleakClient
from typing import Union
from fastapi import FastAPI
from collections import defaultdict
import json as JSON
import time

# TODO: We need to refine the queue and shared dict a bit, this is a basic implementation

# Shared sensor data store
sensor_data = defaultdict(dict)
sensor_queue = asyncio.Queue()

app = FastAPI()

async def indication_handler(sender, data):
    value = int.from_bytes(data, byteorder='little')
    # Put the new sensor data into the queue
    await sensor_queue.put({"sender": sender, "value": value})
    print(f"Indication from {sender}: {value}")

async def ble_manager():
    # Scan for devices, we can set a timeout if necessary
    devices = None

    # Scan indefinitely until we find devices
    while devices is None or len(devices) == 0:
        print("Scanning for devices...")
        devices = await BleakScanner.discover()

    # Work with the first device that matches our criteria
    for device in devices:
        if device.name and "LeakSeek" in device.name:
            print(f"Found device: {device.name}, {device.address}")

            # Set vars for service UUID and characteristic UUIDs, we will use these later
            service_uuid = None # Alert Notification Service
            characteristic_uuid = None  # New Alert

            # Connect to the device. Calling BleakClient automatically connects.
            async with BleakClient(device.address) as client:

                # Check if we're connected
                if client.is_connected:
                    print(f"{device.name} - {device.address} is connected")

                # Explore services
                services = client.services or []

                # Look for the service and characteristic we want
                for service in services:

                    if service.uuid.startswith("00001811"): # Alert Notification Service
                        print(f"{service.uuid}: Found Alert Notification Service")
                        service_uuid = service.uuid # Save the service UUID

                        # Do the same for characteristics
                        for char in service.characteristics:
                            if char.uuid.startswith("00002a3f"): # New Alert
                                characteristic_uuid = char.uuid # Save the characteristic UUID
                
                # If we found the service and characteristic, read the characteristic
                if service_uuid and characteristic_uuid:
                    print(f"  Characteristic: {characteristic_uuid}, Properties: {char.properties}")
                    char_data = await client.read_gatt_char(characteristic_uuid)
                    char_value = int.from_bytes(char_data, byteorder='little')
                    print(f"    Value: {char_value}")
                else:
                    print("Could not find the service or characteristic.")
                    return

                # Subscribe to indications from the characteristic
                await client.start_notify(characteristic_uuid, indication_handler)
                print("Subscribed to indications. Waiting....")

                # Keep the script running to receive indications
                while True:
                    # Get the latest sensor update from the queue
                    sensor_update = await sensor_queue.get()
                    # Update the shared sensor data store
                    sensor_data[device.address] = {
                        "value": sensor_update["value"],
                        "timestamp": time.time()
                    }

                    # give some time before checking connection status again
                    await asyncio.sleep(1)
                    if not client.is_connected:
                        print("Device disconnected.")
                        break

@app.on_event("startup")
async def startup_event():
    asyncio.create_task(ble_manager())

@app.get("/sensor_data")
async def get_sensor_data():
    return dict(sensor_data)