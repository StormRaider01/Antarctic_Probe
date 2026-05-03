"""
ble_client.py
Low-level BLE operations: scan, connect, read GATT characteristics, handle notifications.
Used internally by probe_interface.py — not called directly by GUI.
"""

import asyncio
import logging
from bleak import BleakScanner, BleakClient
from bleak.backends.characteristic import BleakGATTCharacteristic

logger = logging.getLogger(__name__)

# ── GATT Characteristic UUIDs ─────────────────────────────────────────────
# Must match UUIDs defined in ble_server.cpp on the ESP32-C6.
UUID_METADATA = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
UUID_CONTROL  = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
UUID_DATA     = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
UUID_STATUS   = "6e400004-b5a3-f393-e0a9-e50e24dcca9e"

PROBE_NAME     = "UART Service"  # Must match advertised name in firmware
SCAN_TIMEOUT_S = 30.0


async def find_probe() -> str | None:
    """Scan for the probe and return its BLE address, or None if not found."""
    logger.info("Scanning for probe (timeout=%ss)...", SCAN_TIMEOUT_S)
    devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT_S)
    for d in devices:
        if d.name and PROBE_NAME in d.name:
            logger.info("Found probe: %s (%s)", d.name, d.address)
            return d.address
    logger.warning("Probe not found.")
    return None


async def connect(address: str) -> BleakClient:
    client = BleakClient(address)
    await client.connect()
    if not client.is_connected:
        raise ConnectionError(f"Failed to connect to {address}")
    logger.info("Connected to probe at %s", address)
    return client


async def disconnect(client: BleakClient) -> None:
    if client.is_connected:
        await client.disconnect()
        logger.info("Disconnected.")


async def read_metadata(client: BleakClient) -> bytes:
    data = await client.read_gatt_char(UUID_METADATA)
    logger.debug("METADATA raw: %s", data.hex())
    return bytes(data)


async def send_control(client: BleakClient, command: bytes) -> None:
    """Write a command to CONTROL characteristic. b'\\x01' = begin transfer."""
    await client.write_gatt_char(UUID_CONTROL, command, response=True)
    logger.debug("CONTROL write: %s", command.hex())


async def read_status(client: BleakClient) -> bytes:
    data = await client.read_gatt_char(UUID_STATUS)
    logger.debug("STATUS raw: %s", data.hex())
    return bytes(data)


async def stream_data(client: BleakClient, on_packet: callable) -> None:
    """
    Subscribe to DATA notifications. Calls on_packet(raw_bytes) per packet.
    Probe sends a zero-length packet as end-of-transfer sentinel.
    """
    done = asyncio.Event()

    def _handler(char: BleakGATTCharacteristic, data: bytearray) -> None:
        raw = bytes(data)
        if len(raw) == 0:
            done.set()
            return
        on_packet(raw)

    await client.start_notify(UUID_DATA, _handler)
    await send_control(client, b'\x01')   # tell probe to start sending
    await done.wait()
    await client.stop_notify(UUID_DATA)
    logger.info("Data stream complete.")