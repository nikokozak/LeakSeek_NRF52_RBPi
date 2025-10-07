#!/usr/bin/env python3
"""
Diagnostic script to test BLE connection on Raspberry Pi
Tests the specific issue with connecting to LeakSeek devices
"""

import asyncio
from bleak import BleakScanner, BleakClient

async def test_connection():
    print("=== BLE Connection Diagnostic ===\n")
    
    # Step 1: Discover devices
    print("Step 1: Discovering devices (5 seconds)...")
    devices = await BleakScanner.discover(timeout=5.0)
    
    print(f"Found {len(devices)} devices:")
    leakseek_device = None
    
    for device in devices:
        if device.name and "LeakSeek" in device.name:
            print(f"  ✓ {device.name} ({device.address}) - ADDRESS TYPE: {device.details.get('props', {}).get('AddressType', 'unknown')}")
            leakseek_device = device
        elif device.name:
            print(f"    {device.name} ({device.address})")
    
    if not leakseek_device:
        print("\n✗ No LeakSeek device found!")
        print("Make sure the device is powered on and advertising.")
        return
    
    print(f"\n✓ Target device: {leakseek_device.name} ({leakseek_device.address})")
    
    # Step 2: Try connection using device object
    print(f"\nStep 2: Attempting connection...")
    try:
        client = BleakClient(leakseek_device, timeout=10.0)
        print("  - BleakClient created")
        
        await client.connect()
        print("  - connect() called")
        
        if client.is_connected:
            print(f"  ✓ CONNECTED!")
            
            # List services
            print("\n  Services:")
            for service in client.services:
                print(f"    {service.uuid}")
                for char in service.characteristics:
                    print(f"      └─ {char.uuid} ({char.properties})")
            
            await client.disconnect()
            print("\n✓ Test successful - connection works!")
        else:
            print("  ✗ is_connected returned False")
            
    except asyncio.TimeoutError:
        print("  ✗ Connection timed out")
    except Exception as e:
        print(f"  ✗ Error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    print("Starting BLE diagnostic...\n")
    asyncio.run(test_connection())

