import asyncio
from bleak import BleakScanner, BleakClient

async def test_connection():
    devices = await BleakScanner.discover()
    if not devices:
        print("No devices found")
        return
    for device in devices:
        if device.name and "LeakSeek" in device.name:
            print(f"Found device: {device.name}, {device.address}")
            async with BleakClient(device.address) as client:
                print(f"Connected: {client.is_connected}")
                services = client.services or []
                for service in services:
                    if service.uuid.startswith("00001811"):
                        print(f"Found Alert Notification Service: {service.uuid}")
                        for char in service.characteristics:
                            print(f"  Characteristic: {char.uuid}, Properties: {char.properties}")
                            char_data = await client.read_gatt_char(char.uuid)
                            char_value = int.from_bytes(char_data, byteorder='little')
                            print(f"    Value: {char_value}")

asyncio.run(test_connection())