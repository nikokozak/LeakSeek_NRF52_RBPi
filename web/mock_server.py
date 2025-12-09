#!/usr/bin/env python3
"""
Mock server for LeakSeek web UI preview.
Run this to preview the captive portal locally with fake sensor data.

Usage: python3 mock_server.py
Then open: http://localhost:8080
"""

import http.server
import json
import time
import os
from urllib.parse import urlparse, parse_qs

PORT = 8080

# Mock data
registered_devices = [
    {"name": "Head Compartment", "address": "AA:BB:CC:DD:EE:01"},
    {"name": "Stern Locker", "address": "AA:BB:CC:DD:EE:02"},
    {"name": "Engine Bay", "address": "AA:BB:CC:DD:EE:03"},
    {"name": "Forward Cabin", "address": "AA:BB:CC:DD:EE:04"},
    {"name": "Bilge Pump", "address": "AA:BB:CC:DD:EE:05"},
    {"name": "Engine Temp", "address": "AA:BB:CC:DD:EE:06"},
    {"name": "Fuel Tank Pressure", "address": "AA:BB:CC:DD:EE:07"},
]

discovered_devices = [
    {"name": "LeakSeek", "address": "AA:BB:CC:DD:EE:08"},
    {"name": "LeakSeek", "address": "AA:BB:CC:DD:EE:09"},
]

# Sensor data: 0 = dry, 1+ = leak detected (for leak sensors)
# Temp/pressure sensors show relevant values
sensor_data = {
    "AA:BB:CC:DD:EE:01": {"value": 1, "timestamp": time.time(), "stale": False},  # Head - LEAK!
    "AA:BB:CC:DD:EE:02": {"value": 0, "timestamp": time.time(), "stale": False},  # Stern Locker - OK
    "AA:BB:CC:DD:EE:03": {"value": 0, "timestamp": time.time(), "stale": False},  # Engine Bay - OK
    "AA:BB:CC:DD:EE:04": {"value": 0, "timestamp": time.time() - 30, "stale": True},  # Forward Cabin - Offline
    "AA:BB:CC:DD:EE:05": {"value": 0, "timestamp": time.time(), "stale": False},  # Bilge Pump - OK
    "AA:BB:CC:DD:EE:06": {"value": 185, "timestamp": time.time(), "stale": False},  # Engine Temp - 185°F
    "AA:BB:CC:DD:EE:07": {"value": 42, "timestamp": time.time(), "stale": False},  # Fuel Pressure - 42 PSI
}


class MockHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path

        # API endpoints
        if path == "/sensor_data":
            self.send_json(sensor_data)
        elif path.startswith("/sensor_data/"):
            address = path.split("/")[-1]
            if address in sensor_data:
                self.send_json(sensor_data[address])
            else:
                self.send_json({"error": "not found"}, 404)
        elif path == "/registered_devices":
            self.send_json(registered_devices)
        elif path == "/discovered_devices":
            self.send_json(discovered_devices)
        elif path == "/connected_devices":
            self.send_json(registered_devices)
        else:
            # Serve static files
            super().do_GET()

    def do_POST(self):
        parsed = urlparse(self.path)
        path = parsed.path
        query = parse_qs(parsed.query)

        if path.startswith("/register/"):
            address = path.split("/")[-1]
            name = query.get("name", ["Unnamed"])[0]
            registered_devices.append({"name": name, "address": address})
            sensor_data[address] = {"value": 0, "timestamp": time.time(), "stale": False}
            self.send_json({"status": "ok", "address": address, "name": name})

        elif path.startswith("/unregister/"):
            address = path.split("/")[-1]
            # Remove device from list
            for i, d in enumerate(registered_devices):
                if d["address"] == address:
                    registered_devices.pop(i)
                    break
            self.send_json({"status": "ok", "address": address})

        elif path == "/unregister_all":
            registered_devices.clear()
            self.send_json({"status": "ok"})

        elif path.startswith("/rename/"):
            address = path.split("/")[-1]
            new_name = query.get("new_name", [""])[0]
            for device in registered_devices:
                if device["address"] == address:
                    device["name"] = new_name
                    break
            self.send_json({"status": "ok", "address": address, "new_name": new_name})

        else:
            self.send_json({"error": "not found"}, 404)

    def send_json(self, data, status=200):
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(json.dumps(data).encode())

    def log_message(self, format, *args):
        # Only log API calls, not static files
        if args[0].startswith("GET /sensor") or args[0].startswith("POST"):
            print(f"[API] {args[0]}")


if __name__ == "__main__":
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    print(f"""
╔═══════════════════════════════════════════════╗
║  LeakSeek Mock Server                         ║
╠═══════════════════════════════════════════════╣
║  Open: http://localhost:{PORT}                  ║
║                                               ║
║  Mock sensors:                                ║
║  • Engine Bay     - OK (dry)                  ║
║  • Bilge Pump     - LEAK DETECTED!            ║
║  • Forward Cabin  - Offline (stale)           ║
║                                               ║
║  Press Ctrl+C to stop                         ║
╚═══════════════════════════════════════════════╝
""")

    with http.server.HTTPServer(("", PORT), MockHandler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down...")
