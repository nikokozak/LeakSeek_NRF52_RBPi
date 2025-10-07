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

# Scanner control
active_scanner = None  # Reference to current scanner
scanner_lock = threading.Lock()

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
    global discovered_devices, active_scanner
    stop_event = asyncio.Event()
    newly_discovered_devices = []
    all_devices_seen = []

    async def stop_after_timeout():
        await asyncio.sleep(timeout)
        # Compare newly discovered devices and global list,
        # Remove any elements in global list that are not in newly discovered list
        with data_lock:
            for device in discovered_devices[:]:
                if not any(d.address == device.address for d in newly_discovered_devices):
                    discovered_devices.remove(device)
        print(f"Scan timeout - saw {len(all_devices_seen)} total devices, {len(newly_discovered_devices)} matching '{device_name}'")
        stop_event.set()

    asyncio.create_task(stop_after_timeout())

    def detection_callback(device, _advertisement_data):
        # Log ALL devices seen for debugging
        if device not in all_devices_seen:
            all_devices_seen.append(device)
            print(f"BLE device detected: {device.name or 'Unknown'} ({device.address})")
        
        if device.name and device_name in device.name:
            if not any(d.address == device.address for d in newly_discovered_devices):
                newly_discovered_devices.append(device)

            with data_lock:
                if not any(d.address == device.address for d in discovered_devices):
                    discovered_devices.append(device)

                    # If the device is registered, schedule connection (don't block scanner)
                    if utils.device_exists(device.address):
                        print(f"*** Discovered registered device: {device.name}, {device.address}")
                        # Connection will happen after scan completes
                    else:
                        print(f"*** Discovered new device: {device.name}, {device.address}")

    print(f"Starting BLE scan for devices containing '{device_name}'...")
    scanner_obj = BleakScanner(detection_callback)
    
    with scanner_lock:
        active_scanner = scanner_obj
    
    print("Scanner created, calling start()...")
    await scanner_obj.start()
    print("Scanner started successfully, waiting for timeout...")
    
    await stop_event.wait()
    
    print("Timeout reached, stopping scanner...")
    try:
        await scanner_obj.stop()
        print("Scanner stopped")
    except Exception as e:
        print(f"Warning: Error stopping scanner: {e}")
        # Scanner stop failed, but we'll continue anyway
    finally:
        with scanner_lock:
            active_scanner = None

'''
async def connect_to_device(address: str) -> Union[BleakClient, None]:
Connects to a device by address, returns the BleakClient instance if successful
Also subscribes to indications from a specific characteristic
'''
async def connect_to_device(address: str) -> Union[BleakClient, None]:
    global active_scanner
    
    print(f"[{address}] Attempting to connect...")
    
    # Stop scanning before connecting (BlueZ limitation)
    scanner_to_stop = None
    with scanner_lock:
        if active_scanner:
            print(f"[{address}] Pausing scanner for connection...")
            scanner_to_stop = active_scanner
            active_scanner = None
    
    if scanner_to_stop:
        try:
            await scanner_to_stop.stop()
            print(f"[{address}] Scanner paused")
        except Exception as e:
            print(f"[{address}] Warning: Failed to stop scanner: {e}")
        # Give BlueZ a moment to clean up
        await asyncio.sleep(1)
    
    try:
        # First, discover the device to register it with BlueZ
        print(f"[{address}] Discovering device...")
        discovered_device = None
        
        async with BleakScanner() as scanner:
            await asyncio.sleep(3)  # Scan for 3 seconds
            devices = await scanner.discover()
            for d in devices:
                if d.address.upper() == address.upper():
                    discovered_device = d
                    print(f"[{address}] ✓ Device found in discovery")
                    break
        
        if not discovered_device:
            print(f"[{address}] ✗ Device not found in discovery scan")
            return None
        
        # Small delay after discovery
        await asyncio.sleep(0.5)
        
        print(f"[{address}] Creating BleakClient (timeout=15s)...")
        
        # Connect using the discovered device object (not just address)
        try:
            client = BleakClient(discovered_device, timeout=15.0)
            await asyncio.wait_for(client.connect(), timeout=20.0)
        except asyncio.TimeoutError:
            print(f"[{address}] ✗ Connection timed out after 20 seconds")
            return None
        
        try:
            if client.is_connected:
                print(f"[{address}] ✓ Connected successfully")
                with data_lock:
                    connected_devices[address] = client

                services = client.services or []
                print(f"[{address}] Found {len(services)} services")
                
                service_uuid = None # Alert Notification Service
                characteristic_uuid = None  # New Alert

                # Look for the service and characteristic we want
                for service in services:
                    print(f"[{address}]   Service: {service.uuid}")
                    
                    if service.uuid.startswith("00001811"): # Alert Notification Service
                        print(f"[{address}] ✓ Found Alert Notification Service")
                        service_uuid = service.uuid # Save the service UUID

                        # Do the same for characteristics
                        for char in service.characteristics:
                            print(f"[{address}]     Characteristic: {char.uuid}, Properties: {char.properties}")
                            if char.uuid.startswith("00002a3f"): # New Alert
                                characteristic_uuid = char.uuid # Save the characteristic UUID
                                print(f"[{address}] ✓ Found New Alert characteristic")
                
                # If we found the service and characteristic, read the characteristic
                if service_uuid and characteristic_uuid:
                    print(f"[{address}] Reading initial value from characteristic...")
                    try:
                        char_data = await client.read_gatt_char(characteristic_uuid)
                        char_value = int.from_bytes(char_data, byteorder='little')
                        print(f"[{address}] Initial value: {char_value}")
                    except Exception as e:
                        print(f"[{address}] ⚠ Could not read initial value: {e}")
                else:
                    print(f"[{address}] ✗ Could not find the service or characteristic")
                    return

                # Subscribe to indications from the characteristic
                print(f"[{address}] Subscribing to indications...")
                try:
                    await client.start_notify(characteristic_uuid, indication_handler)
                    print(f"[{address}] ✓ Subscribed to indications, waiting for data...")
                except Exception as e:
                    print(f"[{address}] ✗ Failed to subscribe: {e}")
                    return

                # Keep the script running to receive indications
                while True:
                    # Get the latest sensor update from the queue
                    sensor_update = await sensor_queue.get()
                    print(f"[{address}] Received sensor update: {sensor_update['value']}")
                    
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
                        print(f"[{address}] Device disconnected")
                        break

            else:
                print(f"[{address}] ✗ Failed to connect")
                await client.disconnect()
                return None
        finally:
            # Ensure we disconnect
            try:
                await client.disconnect()
            except:
                pass
                
    except Exception as e:
        print(f"[{address}] ✗ Connection error: {e}")
        import traceback
        traceback.print_exc()
        return None

async def main():
    global sensor_queue
    # Initialize the queue in the async context
    sensor_queue = asyncio.Queue()
    
    print("Starting BLE scanner loop...")
    
    # Give BlueZ time to initialize
    await asyncio.sleep(2)
    
    consecutive_errors = 0
    
    while True:
        try:
            print("Starting scan...")
            await scanner(5)
            print("Scan completed")
            consecutive_errors = 0  # Reset error counter on success
            # Longer delay between scans to let BlueZ fully clean up
            await asyncio.sleep(2)
        except Exception as e:
            consecutive_errors += 1
            print(f"Scanner error ({consecutive_errors} consecutive): {e}")
            
            # If we get 3 errors in a row, reset Bluetooth
            if consecutive_errors >= 3:
                print("!!! Too many consecutive errors, resetting Bluetooth...")
                try:
                    import subprocess
                    subprocess.run(['sudo', 'systemctl', 'restart', 'bluetooth'], check=True)
                    await asyncio.sleep(3)
                    # Re-initialize
                    subprocess.run(['sudo', 'rfkill', 'unblock', 'bluetooth'], check=True)
                    subprocess.run(['sudo', 'hciconfig', 'hci0', 'up'], check=True)
                    await asyncio.sleep(2)
                    consecutive_errors = 0
                    print("Bluetooth reset complete, resuming...")
                except Exception as reset_error:
                    print(f"Failed to reset Bluetooth: {reset_error}")
            
            # Wait before retrying
            print("Waiting 5 seconds before retry...")
            await asyncio.sleep(5)

'''
Run the asyncio event loop in a background thread
This allows Bleak to operate asynchronously while Flask handles HTTP synchronously
'''
def run_async_loop():
    global loop
    try:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        print("Event loop created, starting main()...")
        loop.run_until_complete(main())
    except Exception as e:
        print(f"FATAL ERROR in async loop: {e}")
        import traceback
        traceback.print_exc()

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