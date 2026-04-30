"""
probe_interface.py
Public API exposed to Joshua's GUI. This is the only file Joshua's code should import.
Hides all BLE and parsing internals behind simple async functions.
"""

import asyncio
import logging
from pathlib import Path
from dataclasses import asdict

import ble_client
import packet_parser
import data_store

logger = logging.getLogger(__name__)


async def sync_probe(
    db_path: Path = data_store.DEFAULT_DB_PATH,
) -> list[dict]:
    """
    Full sync workflow:
      1. Scan and find probe
      2. Connect
      3. Read metadata
      4. Stream all DATA packets
      5. Parse and delta-decode
      6. Persist to SQLite
      7. Disconnect
    Returns list of record dicts (ready for Joshua's GUI / pandas DataFrame).
    Raises ConnectionError if probe not found.
    """
    address = await ble_client.find_probe()
    if address is None:
        raise ConnectionError("Probe not found. Check reed switch is activated.")

    client = await ble_client.connect(address)
    raw_packets: list[bytes] = []

    try:
        metadata = await ble_client.read_metadata(client)
        logger.info("Metadata received: %s", metadata.hex())

        def _collect(pkt: bytes) -> None:
            raw_packets.append(pkt)

        await ble_client.stream_data(client, _collect)

    finally:
        await ble_client.disconnect(client)

    records = packet_parser.parse_all(raw_packets)

    conn = data_store.init_db(db_path)
    data_store.save_records_sqlite(records, conn)
    conn.close()

    logger.info("Sync complete. %d records transferred.", len(records))
    return [asdict(r) for r in records]


async def get_status() -> dict:
    """
    Connect briefly, read STATUS characteristic, disconnect.
    Returns a dict with raw status bytes for the GUI to display.
    """
    address = await ble_client.find_probe()
    if address is None:
        return {"connected": False, "status_raw": None}

    client = await ble_client.connect(address)
    try:
        status = await ble_client.read_status(client)
    finally:
        await ble_client.disconnect(client)

    return {"connected": True, "status_raw": status.hex()}


# ── Convenience wrapper for non-async callers ─────────────────────────────

def run_sync(db_path: Path = data_store.DEFAULT_DB_PATH) -> list[dict]:
    """Blocking wrapper around sync_probe() for simple scripts / testing."""
    return asyncio.run(sync_probe(db_path))


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    records = run_sync()
    print(f"Synced {len(records)} records.")
    if records:
        print("First record:", records[0])