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

# Shared device store
connected_devices = []
discovered_devices = []

app = FastAPI()

'''
Handle incoming indications from the BLE device
Parses the data and puts it into the sensor_queue for processing
'''
async def indication_handler(sender, data):
    value = int.from_bytes(data, byteorder='little')
    # Put the new sensor data into the queue
    await sensor_queue.put({"sender": sender, "value": value})
    print(f"Indication from {sender}: {value}")

'''
Scan for devices with a specific name, e.g. "LeakSeek"
Operates for a specified timeout period (default 5 seconds)
Populates the global discovered_devices list with found devices
'''
async def scanner(timeout=5.0, device_name="LeakSeek") -> None:
    global discovered_devices
    stop_event = asyncio.Event()
    newly_discovered_devices = []

    async def stop_after_timeout():
        await asyncio.sleep(timeout)
        # Compare newly discovered devices and global list,
        # Remove any elements in global list that are not in newly discovered list
        for device in discovered_devices[:]:
            if not any(d.address == device.address for d in newly_discovered_devices):
                discovered_devices.remove(device)
        stop_event.set()

    asyncio.create_task(stop_after_timeout())

    def detection_callback(device, _advertisement_data):
        if device.name and device_name in device.name:
            if not any(d.address == device.address for d in newly_discovered_devices):
                newly_discovered_devices.append(device)

            if not any(d.address == device.address for d in discovered_devices):
                discovered_devices.append(device)
                print(f"Discovered device: {device.name}, {device.address}")

    async with BleakScanner(detection_callback) as _scanner:
        # Runs continually until timeout
        await stop_event.wait()

'''
async def connect_to_device(address: str) -> Union[BleakClient, None]:
Connects to a device by address, returns the BleakClient instance if successful
Also subscribes to indications from a specific characteristic
'''
async def connect_to_device(address: str) -> Union[BleakClient, None]:
    async with BleakClient(address) as client:
        if client.is_connected:
            print(f"Connected to device at {address}")
            connected_devices.append(client)

            services = client.services or []
            service_uuid = None # Alert Notification Service
            characteristic_uuid = None  # New Alert

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
                sensor_data[client.address] = {
                    "value": sensor_update["value"],
                    "timestamp": time.time()
                }

                # give some time before checking connection status again
                await asyncio.sleep(1)
                if not client.is_connected:
                    connected_devices.remove(client)
                    print("Device disconnected.")
                    break

        else:
            print(f"Failed to connect to device at {address}")
            return None

async def main():
    while True:
        await scanner(5)

@app.on_event("startup")
async def startup_event():
    asyncio.create_task(main())

@app.get("/sensor_data")
async def get_sensor_data():
    return dict(sensor_data)

@app.get("/sensor_data/{address}")
async def get_sensor_data_by_address(address: str):
    if address in sensor_data:
        return sensor_data[address]
    else:
        return {"error": "Device not found"}

@app.get("/discovered_devices")
async def get_discovered_devices():
    devices_info = [{"name": device.name, "address": device.address} for device in discovered_devices]
    return devices_info

@app.get("/connected_devices")
async def get_connected_devices():
    devices_info = [{"name": device.address, "address": device.address} for device in connected_devices]
    return devices_info

@app.post("/connect/{address}")
async def connect(address: str):
    # Check if already connected
    for device in connected_devices:
        if device.address == address:
            return {"status": "already connected", "address": address}
    
    # Attempt to connect to the device
    client = await connect_to_device(address)
    if client:
        return {"status": "connected", "address": address}
    else:
        return {"status": "failed to connect", "address": address}
