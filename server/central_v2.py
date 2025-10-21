import asyncio
from bleak import BleakScanner, BleakClient
from typing import Union
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import Response, FileResponse
from collections import defaultdict
import json as JSON
import time
import utils
import os
import eink_display

# ACK characteristic UUID (must match firmware)
ACK_CHARACTERISTIC_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"

# Manufacturer ID (Konica Minolta, matches firmware)
MANUFACTURER_ID = 0x018B

# Shared sensor data store
# Format: {address: {value, seq, battery, timestamp, needs_ack, last_ack_time}}
sensor_data = defaultdict(dict)

# ACK management
ack_semaphore = asyncio.Semaphore(1)  # Only one ACK connection at a time
ack_in_progress = set()  # Track which devices are being ACKed

# Device store
discovered_devices = []

app = FastAPI()

# Add CORS middleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Mount static files for captive portal
STATIC_DIR = os.path.join(os.path.dirname(__file__), "..", "web")
if os.path.exists(STATIC_DIR):
    app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")

def parse_manufacturer_data(manufacturer_data: dict) -> dict:
    """Parse LeakSeek manufacturer data from advertisement"""
    if MANUFACTURER_ID not in manufacturer_data:
        return None
    
    data = manufacturer_data[MANUFACTURER_ID]
    if len(data) < 4:
        return None
    
    # Validate protocol version
    if data[0] != 1:
        return None
    
    return {
        "protocol": data[0],
        "flags": data[1],
        "seq": data[2],
        "battery": data[3],
        "leak": bool(data[1] & 0x01),
        "needs_ack": bool(data[1] & 0x02)
    }

async def scanner(timeout=5.0, device_name="LeakSeek") -> None:
    """
    Scan for devices with LeakSeek in the name.
    Parse manufacturer data from advertisements.
    Only connect when ACK is needed.
    """
    global discovered_devices
    stop_event = asyncio.Event()
    newly_discovered_devices = []

    async def stop_after_timeout():
        await asyncio.sleep(timeout)
        # Remove stale devices from global list
        for device in discovered_devices[:]:
            if not any(d.address == device.address for d in newly_discovered_devices):
                discovered_devices.remove(device)
        stop_event.set()

    asyncio.create_task(stop_after_timeout())

    def detection_callback(device, advertisement_data):
        """Process each advertisement"""
        if device.name and device_name in device.name:
            # Track discovered devices
            if not any(d.address == device.address for d in newly_discovered_devices):
                newly_discovered_devices.append(device)
            if not any(d.address == device.address for d in discovered_devices):
                discovered_devices.append(device)
                
                if utils.device_exists(device.address):
                    print(f"Discovered registered device: {device.name}, {device.address}")
                else:
                    print(f"Discovered new device: {device.name}, {device.address}")
            
            # Parse manufacturer data
            parsed = parse_manufacturer_data(advertisement_data.manufacturer_data)
            if parsed:
                # Update sensor data
                sensor_data[device.address] = {
                    "value": 1 if parsed["leak"] else 0,
                    "seq": parsed["seq"],
                    "battery": parsed["battery"],
                    "timestamp": time.time(),
                    "needs_ack": parsed["needs_ack"],
                    "last_seen": time.time()
                }
                
                # If ACK needed and not already in queue/progress, enqueue
                if parsed["needs_ack"] and device.address not in ack_in_progress:
                    if utils.device_exists(device.address):
                        print(f"⚠️  ALERT from {device.address}, seq={parsed['seq']}, queuing ACK")
                        ack_in_progress.add(device.address)
                        asyncio.create_task(ack_device(device.address, parsed["seq"]))

    async with BleakScanner(detection_callback) as _scanner:
        await stop_event.wait()

async def ack_device(address: str, seq: int, max_retries: int = 2):
    """
    Connect to device and write ACK to clear alert.
    Uses semaphore to ensure only one connection at a time.
    """
    async with ack_semaphore:
        for attempt in range(max_retries):
            try:
                print(f"ACK attempt {attempt+1}/{max_retries} for {address}, seq={seq}")
                
                # Prefer using BLEDevice object if available for better connection reliability
                device = next((d for d in discovered_devices if d.address == address), None)
                client_target = device if device else address
                
                async with BleakClient(client_target, timeout=10.0) as client:
                    if client.is_connected:
                        # Write the sequence number to ACK characteristic
                        await client.write_gatt_char(
                            ACK_CHARACTERISTIC_UUID,
                            bytes([seq]),
                            response=False
                        )
                        
                        print(f"✓ ACK sent to {address}, seq={seq}")
                        
                        # Update sensor data
                        if address in sensor_data:
                            sensor_data[address]["needs_ack"] = False
                            sensor_data[address]["last_ack_time"] = time.time()
                        
                        # Success - exit retry loop
                        break
                        
            except Exception as e:
                print(f"ACK attempt {attempt+1} failed for {address}: {e}")
                if attempt < max_retries - 1:
                    await asyncio.sleep(1)  # Brief delay before retry
                else:
                    print(f"❌ All ACK attempts failed for {address}")
        
        # Remove from in-progress set
        ack_in_progress.discard(address)

async def eink_update_loop():
    """Periodically update e-ink display"""
    while True:
        await asyncio.sleep(5)  # Check every 5 seconds (reduced for RPi Zero)
        try:
            registered = utils.load_data()
            eink_display.update_display(dict(sensor_data), registered)
        except Exception as e:
            print(f"E-ink update error: {e}")

async def main():
    """Main loop: continuous scanning with error recovery"""
    while True:
        try:
            await scanner(5)
        except Exception as e:
            print(f"❌ Scanner error: {e}")
            await asyncio.sleep(2)  # Brief pause before retry

@app.on_event("startup")
async def startup_event():
    # Initialize e-ink display (with error handling for permission issues)
    try:
        eink_display.init_display()
    except Exception as e:
        print(f"⚠️  E-ink initialization failed: {e}")
        print("   Run with sudo or add user to gpio group")
    
    # Start background tasks
    asyncio.create_task(main())
    asyncio.create_task(eink_update_loop())

@app.on_event("shutdown")
async def shutdown_event():
    eink_display.cleanup_display()

# API Endpoints

@app.get("/")
async def root():
    """Serve the main web interface"""
    index_file = os.path.join(STATIC_DIR, "index.html")
    if os.path.exists(index_file):
        return FileResponse(index_file)
    return {"status": "LeakSeek Server Running"}

@app.get("/{file_path:path}")
async def serve_static(file_path: str):
    """Serve static files (CSS, JS, etc.)"""
    # Skip API routes
    if file_path.startswith(("sensor_data", "discovered_devices", "connected_devices", 
                             "registered_devices", "register", "unregister", "rename", 
                             "ack", "generate_204", "hotspot-detect")):
        return {"error": "Not found"}
    
    file = os.path.join(STATIC_DIR, file_path)
    if os.path.exists(file) and os.path.isfile(file):
        return FileResponse(file)
    return {"error": "File not found"}

@app.get("/generate_204")
async def captive_portal_android():
    """Android captive portal probe"""
    return Response(status_code=204)

@app.get("/hotspot-detect.html")
async def captive_portal_ios():
    """iOS captive portal probe"""
    return Response(
        content='<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>',
        media_type="text/html"
    )

@app.get("/sensor_data")
async def get_sensor_data():
    """Get all sensor data"""
    return dict(sensor_data)

@app.get("/sensor_data/{address}")
async def get_sensor_data_by_address(address: str):
    """Get sensor data for specific address"""
    if address in sensor_data:
        return sensor_data[address]
    else:
        return {"error": "Device not found"}

@app.get("/discovered_devices")
async def get_discovered_devices():
    """Get list of all discovered devices"""
    devices_info = [{"name": device.name, "address": device.address} for device in discovered_devices]
    return devices_info

@app.get("/connected_devices")
async def get_connected_devices():
    """
    Get list of devices currently connected.
    In v2, we don't maintain persistent connections, so return devices with recent ACK activity.
    """
    recent_threshold = time.time() - 10  # Within last 10 seconds
    active_devices = [
        {"name": utils.load_data().get(addr, {}).get("name", addr), "address": addr}
        for addr, data in sensor_data.items()
        if data.get("last_ack_time", 0) > recent_threshold
    ]
    return active_devices

@app.get("/registered_devices")
async def get_registered_devices():
    """Get list of registered devices"""
    devices = utils.load_data()
    return [{"name": device["name"], "address": device["address"]} for device in devices.values()]

@app.post("/register/{address}")
async def register(address: str, name: str):
    """Register a device"""
    device_exists = utils.device_exists(address)
    if device_exists:
        return {"status": "already registered", "address": address, "name": name}
    
    # Add to storage
    utils.add_device(address, name)
    return {"status": "registered", "address": address, "name": name}

@app.post("/unregister/{address}")
async def unregister(address: str):
    """Unregister a device"""
    if utils.device_exists(address):
        utils.remove_device(address)
        
        # Remove from sensor data
        if address in sensor_data:
            del sensor_data[address]
        
        return {"status": "unregistered", "address": address}
    else:
        return {"status": "not found", "address": address}

@app.post("/unregister_all")
async def unregister_all():
    """Unregister all devices"""
    utils.clear_data()
    sensor_data.clear()
    return {"status": "all devices unregistered"}

@app.post("/rename/{address}")
async def rename_device(address: str, new_name: str):
    """Rename a device"""
    if utils.device_exists(address):
        utils.rename_device(address, new_name)
        return {"status": "renamed", "address": address, "new_name": new_name}
    else:
        return {"status": "not found", "address": address}

@app.post("/ack/{address}")
async def manual_ack(address: str):
    """Manually trigger an ACK for a device"""
    if address not in sensor_data:
        return {"status": "error", "message": "Device not found"}
    
    seq = sensor_data[address].get("seq", 0)
    
    if address in ack_in_progress:
        return {"status": "pending", "message": "ACK already in progress"}
    
    ack_in_progress.add(address)
    asyncio.create_task(ack_device(address, seq))
    
    return {"status": "queued", "address": address, "seq": seq}
