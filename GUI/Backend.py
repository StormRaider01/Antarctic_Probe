"""
Backend.py
==========
Pure backend module for the Antarctic Probe GUI.
Contains:
  - Mock async functions (replace with real ESP-NOW/Serial logic later)
  - run_async() helper
  - No GUI imports, no class methods, no 'self' references

GUI.py imports this as `bk` and calls bk.run_async(), bk.mock_get_status(), etc.
"""

import asyncio
import random
import threading
import time

# ── Mock async functions ──────────────────────────────────────────────────
# Replace these with real Serial/ESP-NOW logic once hardware is ready.

async def mock_get_status():
    """Simulates connecting to the probe and reading battery level."""
    await asyncio.sleep(0.5)
    return {
        "connected":   True,
        "battery_pct": random.randint(60, 95),
        "status_raw":  "OK",
    }


async def mock_sync_probe(db_path=None, on_log=None):
    """
    Simulates an ESP-NOW data retrieval session.
    Records match ProbeRecord_t fields: sequence, timestamp_ms,
    temperature, pressure, excitation, fluorescence.

    In production, replace with:
      - Open Serial port to receiver dongle
      - Send RETRIEVE command
      - Read DATA: CSV lines until [SESSION] EOF
      - Parse into the same dict structure
    """
    records = []
    # Simulate groups: NUM_GROUPS sample groups of SAMPLES_PER_GROUP readings each
    NUM_GROUPS       = random.randint(3, 6)
    SAMPLES_PER_GROUP = 15
    GROUP_INTERVAL_MS = 120_000   # 2 min between groups

    total = NUM_GROUPS * SAMPLES_PER_GROUP

    if on_log:
        on_log(f"ESP-NOW link established. Expecting {total} records ({NUM_GROUPS} groups).")
    await asyncio.sleep(0.6)

    seq = 0
    for g in range(NUM_GROUPS):
        group_start_ms = g * GROUP_INTERVAL_MS
        # One set of 'constant' values per group (temp and pressure don't change mid-sample)
        temp     = round(random.uniform(-2.0, 4.0), 2)
        pressure = round(random.uniform(10.0, 200.0), 2)

        for i in range(SAMPLES_PER_GROUP):
            await asyncio.sleep(0.05)
            ts_ms = group_start_ms + i          # 1 ms between readings in group
            record = {
                "sequence":     seq,
                "timestamp_ms": ts_ms,
                "temperature":  temp,
                "pressure":     pressure,
                "excitation":   round(random.uniform(500.0, 530.0), 2),
                "fluorescence": round(random.uniform(120.0, 160.0), 2),
            }
            records.append(record)
            if on_log:
                on_log(f"  Packet {seq + 1}/{total}  [group={g+1}, seq={seq}]")
            seq += 1

        if on_log:
            on_log(f"Group {g + 1}/{NUM_GROUPS} received.")
        await asyncio.sleep(0.1)

    await asyncio.sleep(0.2)
    if on_log:
        on_log(f"Transfer complete. {total} records synced.")
    return records


# ── run_async ─────────────────────────────────────────────────────────────
def run_async(coro, callback=None):
    """
    Run an async coroutine in a background thread without blocking the GUI.
    callback(result, error) is called from that thread when done;
    GUI handlers use self.after(0, ...) to push UI updates back to mainthread.
    """
    def _thread():
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            result = loop.run_until_complete(coro)
            if callback:
                callback(result, None)
        except Exception as exc:
            if callback:
                callback(None, exc)
        finally:
            loop.close()

    t = threading.Thread(target=_thread, daemon=True)
    t.start()
