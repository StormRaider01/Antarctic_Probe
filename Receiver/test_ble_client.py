# test_ble_client.py

import asyncio
import ble_client

async def main():
    print("Looking for probe")
    addr = await ble_client.find_probe()
    if addr:
        client = await ble_client.connect(addr)
        meta = await ble_client.read_metadata(client)
        print("Metadata:", meta.hex())
        await ble_client.disconnect(client)
    else:
        print("Probe not found")

asyncio.run(main())