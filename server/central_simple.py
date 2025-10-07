import asyncio
from bleak import BleakScanner, BleakClient
from typing import Union
from flask import Flask, jsonify, request, send_from_directory
from flask_cors import CORS
from collections import defaultdict
import time
import threading
import utils

# Shared data stores
sensor_data = defaultdict(dict)
sensor_queue = None
connected_devices = {}
discovered_devices = []
device_cache = {}  # Cache BLEDevice objects by address for connection

# Thread-safe locks
data_lock = threading.Lock()
scan_lock = threading.Lock()  # Prevent concurrent scans
scan_in_progress = False

# Event loop for async operations
loop = None

app = Flask(__name__, static_folder='../web', static_url_path='')
CORS(app, resources={r"/*": {"origins": "*"}})

# =============================================================================
# BLE Functions
# =============================================================================

async def indication_handler(sender, data):
    """Handle incoming indications from BLE device"""
    value = int.from_bytes(data, byteorder='little')
    await sensor_queue.put({"sender": sender, "value": value})

async def scan_once(timeout=5.0, device_name="LeakSeek"):
    """
    Perform a single BLE scan and cache device objects.
    Only called when explicitly requested, not continuously.
    """
    global discovered_devices, device_cache
    
    print(f"Starting scan for '{device_name}' devices...")
    
    try:
        devices_found = await BleakScanner.discover(timeout=timeout)
        
        newly_discovered = []
        with data_lock:
            # Don't clear cache - accumulate devices
            for device in devices_found:
                # Cache ALL devices for potential connection
                device_cache[device.address] = device
                
                # Track LeakSeek devices
                if device.name and device_name in device.name:
                    # Only add if not already in list
                    if not any(d.address == device.address for d in discovered_devices):
                        discovered_devices.append(device)
                    newly_discovered.append(device)
                    print(f"  Found: {device.name} ({device.address})")
            
        print(f"Scan complete: {len(newly_discovered)} LeakSeek devices found")
        return newly_discovered
        
    except Exception as e:
        print(f"Scan error: {e}")
        return []

async def connect_to_device(address: str):
    """Connect to a device using cached BLEDevice object"""
    print(f"[{address}] Connection requested")
    
    # Get cached device object
    with data_lock:
        device_obj = device_cache.get(address)
    
    if not device_obj:
        print(f"[{address}] ✗ Device not in cache, please scan first")
        return None
    
    try:
        print(f"[{address}] Connecting...")
        client = BleakClient(device_obj)
        await client.connect()
        
        if not client.is_connected:
            print(f"[{address}] ✗ Connection failed")
            return None
        
        print(f"[{address}] ✓ Connected")
        with data_lock:
            connected_devices[address] = client
        
        # Find and subscribe to characteristic
        service_uuid = "00001811"  # Alert Notification Service
        char_uuid = "00002a3f"  # New Alert
        
        for service in client.services:
            if service.uuid.startswith(service_uuid):
                for char in service.characteristics:
                    if char.uuid.startswith(char_uuid):
                        print(f"[{address}] Subscribing to notifications...")
                        await client.start_notify(char.uuid, indication_handler)
                        print(f"[{address}] ✓ Subscribed")
                        
                        # Keep connection alive and process data
                        while client.is_connected:
                            sensor_update = await sensor_queue.get()
                            with data_lock:
                                sensor_data[address] = {
                                    "value": sensor_update["value"],
                                    "timestamp": time.time()
                                }
                            print(f"[{address}] Data: {sensor_update['value']}")
                        
                        print(f"[{address}] Disconnected")
                        with data_lock:
                            if address in connected_devices:
                                del connected_devices[address]
                        return
        
        print(f"[{address}] ✗ Service/characteristic not found")
        await client.disconnect()
        
    except Exception as e:
        print(f"[{address}] ✗ Error: {e}")
        return None

async def auto_reconnect_loop():
    """
    Simple loop that checks if registered devices need reconnection.
    Does NOT scan - only attempts to reconnect to devices we already know about.
    """
    print("Auto-reconnect loop starting...")
    
    while True:
        try:
            await asyncio.sleep(10)  # Check every 10 seconds
            
            # Check registered devices
            registered = utils.load_data()
            
            with data_lock:
                for address, device_info in registered.items():
                    # If registered but not connected, and we have it in cache
                    if address not in connected_devices and address in device_cache:
                        print(f"Attempting to reconnect to {device_info['name']}...")
                        asyncio.create_task(connect_to_device(address))
                        
        except Exception as e:
            print(f"Auto-reconnect error: {e}")
            await asyncio.sleep(5)

async def main():
    """Main BLE event loop - just keeps things running"""
    global sensor_queue
    sensor_queue = asyncio.Queue()
    
    print("BLE system ready - scan on-demand via API")
    
    # Start auto-reconnect loop
    await auto_reconnect_loop()

# =============================================================================
# Threading
# =============================================================================

def run_async_loop():
    """Run asyncio loop in background thread"""
    global loop
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    loop.run_until_complete(main())

def start_ble_background():
    """Start BLE thread"""
    thread = threading.Thread(target=run_async_loop, daemon=True)
    thread.start()
    print("BLE background thread started")

def schedule_coroutine(coro):
    """Schedule a coroutine in the async thread"""
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
        return jsonify({"error": "Device not found"}), 404

@app.route("/discovered_devices", methods=['GET'])
def get_discovered_devices():
    with data_lock:
        devices_info = [{"name": d.name, "address": d.address} for d in discovered_devices]
    return jsonify(devices_info)

@app.route("/scan", methods=['POST'])
def trigger_scan():
    """Trigger an on-demand BLE scan"""
    global scan_in_progress
    
    # Check if scan already in progress
    with scan_lock:
        if scan_in_progress:
            return jsonify({
                "status": "busy",
                "message": "Scan already in progress"
            }), 409
        scan_in_progress = True
    
    try:
        timeout = float(request.args.get('timeout', 5.0))
        
        if not (loop and loop.is_running()):
            return jsonify({"status": "error", "error": "BLE system not ready"}), 503
        
        # Schedule scan in the async loop
        future = asyncio.run_coroutine_threadsafe(scan_once(timeout), loop)
        
        # Wait for scan to complete (with longer timeout for safety)
        devices = future.result(timeout=timeout + 3)
        
        return jsonify({
            "status": "complete",
            "devices_found": len(devices) if devices else 0
        })
        
    except TimeoutError:
        return jsonify({
            "status": "timeout",
            "message": "Scan took too long"
        }), 408
    except Exception as e:
        return jsonify({
            "status": "error",
            "error": str(e)
        }), 500
    finally:
        with scan_lock:
            scan_in_progress = False

@app.route("/connected_devices", methods=['GET'])
def get_connected_devices():
    with data_lock:
        devices_info = []
        for addr, client in connected_devices.items():
            # Find device name from cache
            name = device_cache.get(addr, type('obj', (object,), {'name': 'Unknown'})).name
            devices_info.append({"name": name, "address": addr})
    return jsonify(devices_info)

@app.route("/registered_devices", methods=['GET'])
def get_registered_devices():
    devices = utils.load_data()
    return jsonify([{"name": device["name"], "address": device["address"]} for device in devices.values()])

@app.route("/register/<address>", methods=['POST'])
def register(address):
    name = request.args.get('name', 'Unnamed Device')
    
    if utils.device_exists(address):
        return jsonify({"status": "already registered", "address": address, "name": name})
    
    utils.add_device(address, name)
    schedule_coroutine(connect_to_device(address))
    
    return jsonify({"status": "registered", "address": address, "name": name})

@app.route("/unregister/<address>", methods=['POST'])
def unregister(address):
    if not utils.device_exists(address):
        return jsonify({"status": "not found", "address": address}), 404
    
    utils.remove_device(address)
    
    with data_lock:
        if address in connected_devices:
            # Disconnection will happen when the connection loop exits
            pass
    
    return jsonify({"status": "unregistered", "address": address})

@app.route("/unregister_all", methods=['POST'])
def unregister_all():
    utils.clear_data()
    # Connections will drop when devices are no longer registered
    return jsonify({"status": "all devices unregistered"})

@app.route("/rename/<address>", methods=['POST'])
def rename_device_route(address):
    new_name = request.args.get('new_name', '')
    if not utils.device_exists(address):
        return jsonify({"status": "not found", "address": address}), 404
    
    utils.rename_device(address, new_name)
    return jsonify({"status": "renamed", "address": address, "new_name": new_name})

# =============================================================================
# Main
# =============================================================================

if __name__ == '__main__':
    start_ble_background()
    app.run(host='0.0.0.0', port=2300, debug=True, use_reloader=False)

