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
import random
import subprocess
from datetime import datetime
import logging

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

# ACK characteristic UUID (must match firmware)
ACK_CHARACTERISTIC_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"

# Manufacturer ID (Konica Minolta, matches firmware)
MANUFACTURER_ID = 0x018B

# Stale threshold: 2x advertising interval + scan interval = ~20 seconds
STALE_THRESHOLD_SECONDS = 20

# RSSI threshold: Only ACK devices with signal strength above this
MIN_RSSI_FOR_ACK = -80  # dBm, adjust based on environment

# Retry configuration
MAX_ACK_RETRIES = 5
INITIAL_RETRY_DELAY = 1.0  # seconds
MAX_RETRY_DELAY = 30.0  # seconds
SCANNER_RETRY_DELAYS = [2, 5, 10, 20, 30]  # Exponential backoff for scanner

# Bluetooth adapter health check
BT_HEALTH_CHECK_INTERVAL = 60  # seconds
BT_FAILURE_THRESHOLD = 5  # consecutive failures before reset
BT_RESET_COOLDOWN = 10  # seconds after reset before retry

# Watchdog configuration
WATCHDOG_INTERVAL = 30  # seconds
SCANNER_TIMEOUT = 60  # If no scan activity for this long, assume hung

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

# Metrics tracking
metrics = {
    "scanner": {
        "scans_started": 0,
        "scans_completed": 0,
        "scans_failed": 0,
        "last_scan_time": 0,
        "consecutive_failures": 0,
        "adapter_resets": 0,
        "advertisements_received": 0
    },
    "ack": {
        "attempts": 0,
        "successes": 0,
        "failures": 0,
        "retries": 0,
        "total_latency": 0.0,  # sum of all ACK latencies
        "rssi_filtered": 0  # ACKs skipped due to low RSSI
    },
    "alerts": {
        "total": 0,
        "duplicates_filtered": 0
    }
}

# Alert history for deduplication (address -> {seq, timestamp})
alert_history = {}

# Watchdog state
watchdog_state = {
    "last_activity": time.time(),
    "is_healthy": True
}

def load_registered_cached(force=False):
    """Load registered devices from cache or disk"""
    global registered_cache
    if force or not registered_cache:
        registered_cache = utils.load_data()
    return registered_cache

async def reset_bluetooth_adapter():
    """Reset the Bluetooth adapter using hciconfig (requires root/sudo)"""
    try:
        logging.warning("Resetting Bluetooth adapter hci0...")
        # Try to reset without sudo first
        result = subprocess.run(['hciconfig', 'hci0', 'reset'],
                              capture_output=True, text=True, timeout=5)
        if result.returncode == 0:
            logging.info("Bluetooth adapter reset successfully")
            metrics["scanner"]["adapter_resets"] += 1
            return True
        else:
            logging.error(f"Failed to reset adapter: {result.stderr}")
            return False
    except Exception as e:
        logging.error(f"Exception resetting Bluetooth adapter: {e}")
        return False

async def check_bluetooth_health():
    """Check if Bluetooth adapter is responsive"""
    try:
        result = subprocess.run(['hciconfig', 'hci0'],
                              capture_output=True, text=True, timeout=2)
        return result.returncode == 0 and "UP RUNNING" in result.stdout
    except Exception as e:
        logging.error(f"Bluetooth health check failed: {e}")
        return False

def is_rssi_acceptable(rssi: int) -> bool:
    """Check if RSSI is strong enough for reliable ACK"""
    return rssi >= MIN_RSSI_FOR_ACK if rssi is not None else True

def is_duplicate_alert(address: str, seq: int) -> bool:
    """Check if this is a duplicate alert (already seen this sequence)"""
    if address not in alert_history:
        return False

    last_seq = alert_history[address].get("seq", -1)
    last_time = alert_history[address].get("timestamp", 0)

    # If same sequence, check if it's within reasonable time window (5 minutes)
    if seq == last_seq and (time.time() - last_time) < 300:
        return True

    return False

def record_alert(address: str, seq: int):
    """Record an alert to prevent duplicate processing"""
    alert_history[address] = {
        "seq": seq,
        "timestamp": time.time()
    }

def calculate_backoff_delay(attempt: int, initial_delay: float, max_delay: float) -> float:
    """Calculate exponential backoff with jitter"""
    delay = min(initial_delay * (2 ** attempt), max_delay)
    # Add jitter: ±20%
    jitter = delay * 0.2 * (random.random() * 2 - 1)
    return max(0.1, delay + jitter)

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
    Enhanced with metrics tracking and watchdog updates.
    """
    global discovered_devices, watchdog_state

    metrics["scanner"]["scans_started"] += 1
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
        """Process each advertisement with enhanced metrics and filtering"""
        if device.name and device_name in device.name:
            # Update watchdog
            watchdog_state["last_activity"] = time.time()
            metrics["scanner"]["advertisements_received"] += 1

            # Track discovered devices
            if not any(d.address == device.address for d in newly_discovered_devices):
                newly_discovered_devices.append(device)
            if not any(d.address == device.address for d in discovered_devices):
                discovered_devices.append(device)

                is_registered = device.address in load_registered_cached()
                rssi_info = f", RSSI={advertisement_data.rssi}" if advertisement_data.rssi else ""
                if is_registered:
                    logging.info(f"Discovered registered device: {device.name}, {device.address}{rssi_info}")
                else:
                    logging.info(f"Discovered new device: {device.name}, {device.address}{rssi_info}")

            # Parse manufacturer data
            parsed = parse_manufacturer_data(advertisement_data.manufacturer_data)
            if parsed:
                # Update sensor data with RSSI
                sensor_data[device.address] = {
                    "value": 1 if parsed["leak"] else 0,
                    "seq": parsed["seq"],
                    "battery": parsed["battery"],
                    "timestamp": time.time(),
                    "needs_ack": parsed["needs_ack"],
                    "last_seen": time.time(),
                    "rssi": advertisement_data.rssi
                }

                # If ACK needed and not already in queue/progress, enqueue
                if parsed["needs_ack"] and device.address not in ack_in_progress:
                    if device.address in load_registered_cached():
                        # Check for duplicate alert
                        if is_duplicate_alert(device.address, parsed["seq"]):
                            metrics["alerts"]["duplicates_filtered"] += 1
                            logging.info(f"Duplicate alert filtered: {device.address}, seq={parsed['seq']}")
                            return

                        # Check RSSI before attempting ACK
                        if not is_rssi_acceptable(advertisement_data.rssi):
                            metrics["ack"]["rssi_filtered"] += 1
                            logging.warning(f"⚠️  ALERT from {device.address} (RSSI={advertisement_data.rssi}) - signal too weak for ACK, waiting for better signal")
                            return

                        metrics["alerts"]["total"] += 1
                        record_alert(device.address, parsed["seq"])
                        logging.warning(f"⚠️  ALERT from {device.address}, seq={parsed['seq']}, RSSI={advertisement_data.rssi}, queuing ACK")
                        ack_in_progress.add(device.address)
                        asyncio.create_task(ack_device(device.address, parsed["seq"]))

    try:
        async with BleakScanner(detection_callback) as _scanner:
            await stop_event.wait()

        metrics["scanner"]["scans_completed"] += 1
        metrics["scanner"]["last_scan_time"] = time.time()
        metrics["scanner"]["consecutive_failures"] = 0  # Reset on success
    except Exception as e:
        metrics["scanner"]["scans_failed"] += 1
        metrics["scanner"]["consecutive_failures"] += 1
        logging.error(f"Scanner exception: {e}")
        raise

async def ack_device(address: str, seq: int, max_retries: int = MAX_ACK_RETRIES):
    """
    Connect to device and write ACK to clear alert.
    Uses semaphore to ensure only one connection at a time.
    Enhanced with exponential backoff, latency tracking, and better error handling.
    """
    async with ack_semaphore:
        start_time = time.time()
        metrics["ack"]["attempts"] += 1

        for attempt in range(max_retries):
            try:
                if attempt > 0:
                    metrics["ack"]["retries"] += 1
                    delay = calculate_backoff_delay(attempt - 1, INITIAL_RETRY_DELAY, MAX_RETRY_DELAY)
                    logging.info(f"ACK retry {attempt+1}/{max_retries} for {address} after {delay:.1f}s delay")
                    await asyncio.sleep(delay)
                else:
                    logging.info(f"ACK attempt {attempt+1}/{max_retries} for {address}, seq={seq}")

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

                        # Calculate latency
                        latency = time.time() - start_time
                        metrics["ack"]["successes"] += 1
                        metrics["ack"]["total_latency"] += latency

                        logging.info(f"✓ ACK sent to {address}, seq={seq}, latency={latency:.2f}s")

                        # Update sensor data
                        if address in sensor_data:
                            sensor_data[address]["needs_ack"] = False
                            sensor_data[address]["last_ack_time"] = time.time()

                        # Success - exit retry loop
                        break

            except Exception as e:
                logging.error(f"ACK attempt {attempt+1} failed for {address}: {type(e).__name__}: {e}")
                if attempt < max_retries - 1:
                    continue
                else:
                    # All attempts failed
                    metrics["ack"]["failures"] += 1
                    logging.error(f"❌ All {max_retries} ACK attempts failed for {address}")

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
            logging.error(f"E-ink update error: {e}")

async def watchdog_loop():
    """Monitor scanner health and alert if hung"""
    while True:
        await asyncio.sleep(WATCHDOG_INTERVAL)

        time_since_activity = time.time() - watchdog_state["last_activity"]

        if time_since_activity > SCANNER_TIMEOUT:
            logging.critical(f"⚠️  WATCHDOG: No scanner activity for {time_since_activity:.0f}s - scanner may be hung!")
            watchdog_state["is_healthy"] = False

            # Could trigger adapter reset here if needed
            if metrics["scanner"]["consecutive_failures"] >= BT_FAILURE_THRESHOLD:
                logging.critical("Watchdog triggering Bluetooth adapter reset...")
                await reset_bluetooth_adapter()
                await asyncio.sleep(BT_RESET_COOLDOWN)
        else:
            # Scanner is healthy
            if not watchdog_state["is_healthy"]:
                logging.info("Watchdog: Scanner activity resumed, marking healthy")
                watchdog_state["is_healthy"] = True

async def main():
    """
    Main loop: continuous scanning with enhanced error recovery.
    Includes exponential backoff, health checks, and adapter reset capability.
    """
    retry_index = 0
    last_health_check = 0

    while True:
        # Periodic Bluetooth health check
        if time.time() - last_health_check > BT_HEALTH_CHECK_INTERVAL:
            is_healthy = await check_bluetooth_health()
            last_health_check = time.time()
            watchdog_state["is_healthy"] = is_healthy

            if not is_healthy:
                logging.error("Bluetooth adapter health check failed")
                metrics["scanner"]["consecutive_failures"] += 1

                # If too many consecutive failures, try adapter reset
                if metrics["scanner"]["consecutive_failures"] >= BT_FAILURE_THRESHOLD:
                    logging.critical(f"Bluetooth adapter appears wedged ({BT_FAILURE_THRESHOLD} consecutive failures), attempting reset...")
                    reset_success = await reset_bluetooth_adapter()
                    if reset_success:
                        await asyncio.sleep(BT_RESET_COOLDOWN)
                        metrics["scanner"]["consecutive_failures"] = 0
                        retry_index = 0  # Reset backoff after adapter reset
                    else:
                        logging.critical("Failed to reset Bluetooth adapter - manual intervention may be required")

        # Pause scanning if ACK in progress to reduce BLE/WiFi interference
        if ack_in_progress:
            await asyncio.sleep(1)
            continue

        try:
            await scanner(8)  # Longer scan, less frequent restarts
            retry_index = 0  # Reset backoff on success

        except Exception as e:
            # Determine retry delay based on error type and retry index
            if "No powered Bluetooth adapters" in str(e):
                logging.error("⚠️  Bluetooth not powered/ready")
            else:
                logging.error(f"❌ Scanner error: {type(e).__name__}: {e}")

            # Use exponential backoff
            if retry_index < len(SCANNER_RETRY_DELAYS):
                delay = SCANNER_RETRY_DELAYS[retry_index]
                retry_index += 1
            else:
                delay = SCANNER_RETRY_DELAYS[-1]  # Max delay

            logging.info(f"Retrying scanner in {delay}s (attempt {retry_index})...")
            await asyncio.sleep(delay)

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
    asyncio.create_task(watchdog_loop())

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

@app.get("/health")
async def get_health():
    """
    Health check endpoint for monitoring system status.
    Returns overall system health and key metrics.
    """
    # Calculate derived metrics
    ack_success_rate = 0.0
    if metrics["ack"]["attempts"] > 0:
        ack_success_rate = metrics["ack"]["successes"] / metrics["ack"]["attempts"]

    avg_ack_latency = 0.0
    if metrics["ack"]["successes"] > 0:
        avg_ack_latency = metrics["ack"]["total_latency"] / metrics["ack"]["successes"]

    scan_success_rate = 0.0
    if metrics["scanner"]["scans_started"] > 0:
        scan_success_rate = metrics["scanner"]["scans_completed"] / metrics["scanner"]["scans_started"]

    # Determine overall health status
    is_healthy = (
        watchdog_state["is_healthy"] and
        metrics["scanner"]["consecutive_failures"] < BT_FAILURE_THRESHOLD and
        (ack_success_rate > 0.8 or metrics["ack"]["attempts"] < 5)  # Allow for initial failures
    )

    return {
        "status": "healthy" if is_healthy else "degraded",
        "watchdog": {
            "is_healthy": watchdog_state["is_healthy"],
            "last_activity": watchdog_state["last_activity"],
            "seconds_since_activity": time.time() - watchdog_state["last_activity"]
        },
        "scanner": {
            **metrics["scanner"],
            "success_rate": scan_success_rate
        },
        "ack": {
            **metrics["ack"],
            "success_rate": ack_success_rate,
            "avg_latency_seconds": avg_ack_latency
        },
        "alerts": metrics["alerts"],
        "active_devices": len([d for d in sensor_data.values() if time.time() - d.get("timestamp", 0) < STALE_THRESHOLD_SECONDS]),
        "total_registered": len(registered_cache)
    }

@app.get("/metrics")
async def get_metrics():
    """
    Prometheus-compatible metrics endpoint.
    Returns metrics in a format suitable for monitoring systems.
    """
    lines = []

    # Scanner metrics
    lines.append(f"# HELP leakseek_scanner_scans_started_total Total number of scans started")
    lines.append(f"# TYPE leakseek_scanner_scans_started_total counter")
    lines.append(f"leakseek_scanner_scans_started_total {metrics['scanner']['scans_started']}")

    lines.append(f"# HELP leakseek_scanner_scans_completed_total Total number of scans completed")
    lines.append(f"# TYPE leakseek_scanner_scans_completed_total counter")
    lines.append(f"leakseek_scanner_scans_completed_total {metrics['scanner']['scans_completed']}")

    lines.append(f"# HELP leakseek_scanner_scans_failed_total Total number of scans failed")
    lines.append(f"# TYPE leakseek_scanner_scans_failed_total counter")
    lines.append(f"leakseek_scanner_scans_failed_total {metrics['scanner']['scans_failed']}")

    lines.append(f"# HELP leakseek_scanner_consecutive_failures Current consecutive failure count")
    lines.append(f"# TYPE leakseek_scanner_consecutive_failures gauge")
    lines.append(f"leakseek_scanner_consecutive_failures {metrics['scanner']['consecutive_failures']}")

    lines.append(f"# HELP leakseek_scanner_adapter_resets_total Number of Bluetooth adapter resets")
    lines.append(f"# TYPE leakseek_scanner_adapter_resets_total counter")
    lines.append(f"leakseek_scanner_adapter_resets_total {metrics['scanner']['adapter_resets']}")

    # ACK metrics
    lines.append(f"# HELP leakseek_ack_attempts_total Total number of ACK attempts")
    lines.append(f"# TYPE leakseek_ack_attempts_total counter")
    lines.append(f"leakseek_ack_attempts_total {metrics['ack']['attempts']}")

    lines.append(f"# HELP leakseek_ack_successes_total Total number of successful ACKs")
    lines.append(f"# TYPE leakseek_ack_successes_total counter")
    lines.append(f"leakseek_ack_successes_total {metrics['ack']['successes']}")

    lines.append(f"# HELP leakseek_ack_failures_total Total number of failed ACKs")
    lines.append(f"# TYPE leakseek_ack_failures_total counter")
    lines.append(f"leakseek_ack_failures_total {metrics['ack']['failures']}")

    # Alert metrics
    lines.append(f"# HELP leakseek_alerts_total Total number of alerts received")
    lines.append(f"# TYPE leakseek_alerts_total counter")
    lines.append(f"leakseek_alerts_total {metrics['alerts']['total']}")

    lines.append(f"# HELP leakseek_alerts_duplicates_filtered_total Number of duplicate alerts filtered")
    lines.append(f"# TYPE leakseek_alerts_duplicates_filtered_total counter")
    lines.append(f"leakseek_alerts_duplicates_filtered_total {metrics['alerts']['duplicates_filtered']}")

    # System health
    lines.append(f"# HELP leakseek_system_healthy System health status (1=healthy, 0=degraded)")
    lines.append(f"# TYPE leakseek_system_healthy gauge")
    lines.append(f"leakseek_system_healthy {1 if watchdog_state['is_healthy'] else 0}")

    return Response(content="\n".join(lines) + "\n", media_type="text/plain")
