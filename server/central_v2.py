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

# Stale threshold: 2x advertising interval + scan interval = ~20 seconds
STALE_THRESHOLD_SECONDS = 20

# Shared sensor data store
# Format: {address: {value, seq, battery, timestamp, needs_ack, last_ack_time}}
sensor_data = defaultdict(dict)

# ACK management
ack_semaphore = asyncio.Semaphore(1)  # Only one ACK connection at a time
ack_in_progress = set()  # Track which devices are being ACKed

# Device store
discovered_devices = []

# Cached registered devices (avoid repeated JSON reads)
registered_cache = {}

def load_registered_cached(force=False):
    """Load registered devices from cache or disk"""
    global registered_cache
    if force or not registered_cache:
        registered_cache = utils.load_data()
    return registered_cache

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

async def scanner(timeout=8.0, device_name="LeakSeek") -> None:
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
                
                is_registered = device.address in load_registered_cached()
                if is_registered:
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
                    if device.address in load_registered_cached():
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
        await asyncio.sleep(10)  # Check every 10 seconds (demo-optimized)
        try:
            registered = load_registered_cached()
            eink_display.update_display(dict(sensor_data), registered)
        except Exception as e:
            print(f"E-ink update error: {e}")

async def main():
    """Main loop: continuous scanning with error recovery"""
    # Wait for Bluetooth to be ready (especially important at boot)
    startup_retry_delay = 5
    
    while True:
        # Pause scanning if ACK in progress to reduce BLE/WiFi interference
        if ack_in_progress:
            await asyncio.sleep(1)
            continue
            
        try:
            await scanner(8)  # Longer scan, less frequent restarts
            startup_retry_delay = 2  # After first success, use shorter retry
        except Exception as e:
            if "No powered Bluetooth adapters" in str(e):
                print(f"⚠️  Bluetooth not ready, retrying in {startup_retry_delay}s...")
            else:
                print(f"❌ Scanner error: {e}")
            await asyncio.sleep(startup_retry_delay)

@app.on_event("startup")
async def startup_event():
    import logging
    logger = logging.getLogger("uvicorn.error")
    
    # Load registered devices cache
    load_registered_cached(force=True)
    logger.info(f"Loaded {len(registered_cache)} registered devices")
    
    # Initialize e-ink display (with error handling for permission issues)
    try:
        logger.info("Attempting to initialize e-ink display...")
        eink_display.init_display()
        logger.info("E-ink display initialized successfully")
    except Exception as e:
        logger.error(f"⚠️  E-ink initialization failed: {e}")
        logger.error("   Run with sudo or add user to gpio group")
        import traceback
        logger.error(traceback.format_exc())
    
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

# Static file routes for CSS/JS
@app.get("/style.css")
async def get_style():
    return FileResponse(os.path.join(STATIC_DIR, "style.css"))

@app.get("/app.js")
async def get_app_js():
    return FileResponse(os.path.join(STATIC_DIR, "app.js"))

@app.get("/api.js")
async def get_api_js():
    return FileResponse(os.path.join(STATIC_DIR, "api.js"))

@app.get("/keyboard.js")
async def get_keyboard_js():
    return FileResponse(os.path.join(STATIC_DIR, "keyboard.js"))

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
    """Get all sensor data with stale detection"""
    current_time = time.time()
    enriched_data = {}
    
    for address, data in sensor_data.items():
        last_seen = data.get("timestamp", 0)
        is_stale = (current_time - last_seen) > STALE_THRESHOLD_SECONDS
        
        enriched_data[address] = {
            **data,
            "stale": is_stale
        }
    
    return enriched_data

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
    registered = load_registered_cached()
    active_devices = [
        {"name": registered.get(addr, {}).get("name", addr), "address": addr}
        for addr, data in sensor_data.items()
        if data.get("last_ack_time", 0) > recent_threshold
    ]
    return active_devices

@app.get("/registered_devices")
async def get_registered_devices():
    """Get list of registered devices"""
    devices = load_registered_cached()
    return [{"name": device["name"], "address": device["address"]} for device in devices.values()]

@app.post("/register/{address}")
async def register(address: str, name: str):
    """Register a device"""
    if address in registered_cache:
        return {"status": "already registered", "address": address, "name": name}
    
    # Add to storage and cache
    utils.add_device(address, name)
    registered_cache[address] = {"name": name, "address": address}
    return {"status": "registered", "address": address, "name": name}

@app.post("/unregister/{address}")
async def unregister(address: str):
    """Unregister a device"""
    if address in registered_cache:
        utils.remove_device(address)
        registered_cache.pop(address, None)
        
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
    registered_cache.clear()
    sensor_data.clear()
    return {"status": "all devices unregistered"}

@app.post("/rename/{address}")
async def rename_device(address: str, new_name: str):
    """Rename a device"""
    if address in registered_cache:
        utils.rename_device(address, new_name)
        registered_cache[address]["name"] = new_name
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
