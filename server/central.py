import asyncio
from bleak import BleakScanner, BleakClient
from typing import Union
from flask import Flask, jsonify, request, send_from_directory
from flask_cors import CORS
from collections import defaultdict
import json as JSON
import time
import threading
import os
import utils

# TODO: We need to refine the queue and shared dict a bit, this is a basic implementation

# Shared sensor data store
sensor_data = defaultdict(dict)
sensor_queue = None  # Will be initialized in the async thread

# Shared device store
connected_devices = {}  # Dict keyed by address for easy lookup
discovered_devices = []

# Thread-safe locks for shared data
data_lock = threading.Lock()

# Event loop for async operations
loop = None
loop_thread = None

app = Flask(__name__, static_folder='../web', static_url_path='')

# Add CORS support to allow web interface
CORS(app, resources={r"/*": {"origins": "*"}})

'''
Handle incoming indications from the BLE device
Parses the data and puts it into the sensor_queue for processing
'''
async def indication_handler(sender, data):
    value = int.from_bytes(data, byteorder='little')
    # Put the new sensor data into the queue
    await sensor_queue.put({"sender": sender, "value": value})
    # print(f"Indication from {sender}: {value}")

'''
Scan for devices with a specific name, e.g. "LeakSeek"
Operates for a specified timeout period (default 5 seconds)
Populates the global discovered_devices list with found devices
If it comes across a registered device, it'll connect to it automatically
'''
async def scanner(timeout=5.0, device_name="LeakSeek") -> None:
    global discovered_devices
    stop_event = asyncio.Event()
    newly_discovered_devices = []

    async def stop_after_timeout():
        await asyncio.sleep(timeout)
        # Compare newly discovered devices and global list,
        # Remove any elements in global list that are not in newly discovered list
        with data_lock:
            for device in discovered_devices[:]:
                if not any(d.address == device.address for d in newly_discovered_devices):
                    discovered_devices.remove(device)
        stop_event.set()

    asyncio.create_task(stop_after_timeout())

    def detection_callback(device, _advertisement_data):
        if device.name and device_name in device.name:
            if not any(d.address == device.address for d in newly_discovered_devices):
                newly_discovered_devices.append(device)

            with data_lock:
                if not any(d.address == device.address for d in discovered_devices):
                    discovered_devices.append(device)

                    # If the device is registered, print a message, and auto-connect
                    if utils.device_exists(device.address):
                        asyncio.create_task(connect_to_device(device.address))
                        print(f"Discovered registered device: {device.name}, {device.address}")
                    
                    else:
                        print(f"Discovered new device: {device.name}, {device.address}")

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
            with data_lock:
                connected_devices[address] = client

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
                with data_lock:
                    sensor_data[client.address] = {
                        "value": sensor_update["value"],
                        "timestamp": time.time()
                    }

                # give some time before checking connection status again
                await asyncio.sleep(1)
                if not client.is_connected:
                    with data_lock:
                        del connected_devices[address]
                    print("Device disconnected.")
                    break

        else:
            print(f"Failed to connect to device at {address}")
            return None

async def main():
    global sensor_queue
    # Initialize the queue in the async context
    sensor_queue = asyncio.Queue()
    
    while True:
        await scanner(5)

'''
Run the asyncio event loop in a background thread
This allows Bleak to operate asynchronously while Flask handles HTTP synchronously
'''
def run_async_loop():
    global loop
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    loop.run_until_complete(main())

'''
Start the background thread for BLE operations
Called when Flask app starts
'''
def start_ble_background():
    global loop_thread
    loop_thread = threading.Thread(target=run_async_loop, daemon=True)
    loop_thread.start()
    print("BLE background thread started")

'''
Helper function to schedule coroutines in the async thread
'''
def schedule_coroutine(coro):
    if loop and loop.is_running():
        asyncio.run_coroutine_threadsafe(coro, loop)

# =============================================================================
# Flask Routes
# =============================================================================

@app.route('/')
def serve_index():
    return send_from_directory(app.static_folder, 'index.html')

@app.route("/<path:path>")
def serve_static(path):
    return send_from_directory(app.static_folder, path)

@app.route("/sensor_data", methods=['GET'])
def get_sensor_data():
    with data_lock:
        return jsonify(dict(sensor_data))

@app.route("/sensor_data/<address>", methods=['GET'])
def get_sensor_data_by_address(address):
    with data_lock:
        if address in sensor_data:
            return jsonify(sensor_data[address])
        else:
            return jsonify({"error": "Device not found"}), 404

@app.route("/discovered_devices", methods=['GET'])
def get_discovered_devices():
    with data_lock:
        devices_info = [{"name": device.name, "address": device.address} for device in discovered_devices]
    return jsonify(devices_info)

@app.route("/connected_devices", methods=['GET'])
def get_connected_devices():
    with data_lock:
        devices_info = [{"name": device.name, "address": device.address} for device in connected_devices.values()]
    return jsonify(devices_info)

@app.route("/registered_devices", methods=['GET'])
def get_registered_devices():
    devices = utils.load_data()
    return jsonify([{"name": device["name"], "address": device["address"]} for device in devices.values()])

@app.route("/register/<address>", methods=['POST'])
def register(address):
    name = request.args.get('name', 'Unnamed Device')
    
    # Check if device is already registered
    device_exists = utils.device_exists(address)
    if device_exists:
        return jsonify({"status": "already registered", "address": address, "name": name})

    # Check if already connected
    with data_lock:
        if address in connected_devices:
            return jsonify({"status": "already connected", "address": address, "name": name})
    
    # Attempt to connect to the device
    schedule_coroutine(connect_to_device(address))
    if not device_exists:
        utils.add_device(address, name)  # Save to local storage
        return jsonify({"status": "connected", "address": address, "name": name})
    else:
        return jsonify({"status": "failed to connect", "address": address})

@app.route("/unregister/<address>", methods=['POST'])
def unregister(address):
    if utils.device_exists(address):
        utils.remove_device(address)
        
        # Disconnect if currently connected
        with data_lock:
            if address in connected_devices:
                client = connected_devices[address]
                # Schedule disconnect in the async loop
                schedule_coroutine(client.disconnect())
        
        return jsonify({"status": "unregistered", "address": address})
    else:
        return jsonify({"status": "not found", "address": address}), 404

@app.route("/unregister_all", methods=['POST'])
def unregister_all():
    utils.clear_data()
    with data_lock:
        for address in list(connected_devices.keys()):
            client = connected_devices[address]
            schedule_coroutine(client.disconnect())
        connected_devices.clear()
    return jsonify({"status": "all devices unregistered"})

@app.route("/rename/<address>", methods=['POST'])
def rename_device(address):
    new_name = request.args.get('new_name', '')
    if utils.device_exists(address):
        utils.rename_device(address, new_name)
        return jsonify({"status": "renamed", "address": address, "new_name": new_name})
    else:
        return jsonify({"status": "not found", "address": address}), 404

# =============================================================================
# Application Entry Point
# =============================================================================

if __name__ == '__main__':
    # Start BLE operations in background thread
    start_ble_background()
    
    # Run Flask app
    # use_reloader=False prevents Flask from spawning twice (breaks background thread)
    # For production on Pi Zero, use: app.run(host='0.0.0.0', port=80, debug=False)
    app.run(host='0.0.0.0', port=2300, debug=True, use_reloader=False)