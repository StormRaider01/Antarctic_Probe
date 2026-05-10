"""
Backend.py
==========
Pure backend module for the Antarctic Probe GUI.
Contains:
  - Serial connection management (connect_dongle)
  - Real sync_probe() reading DATA: lines from the receiver dongle
  - run_async()      — for async coroutines (connect_dongle)
  - run_in_thread()  — for blocking functions (sync_probe)
  - No GUI imports, no class methods, no 'self' references

GUI.py imports this as `bk`.
"""

import asyncio
import logging
import threading
from dataclasses import asdict, dataclass

import serial
from serial.tools import list_ports


# ===========================================================================
# Global variables and config

logger = logging.getLogger(__name__)

_ser: serial.Serial | None = None   # shared Serial instance, owned by Backend

BAUD_RATE        = 115200
DONGLE_READY_MSG = "Ready"          # must match what receiver_dongle.ino prints on boot

@dataclass
class ProbeRecord:
    entry_num:        int
    ms_since_start:   int
    temperature_c:    float
    pressure_dbar:    float
    excitation_raw:   float
    fluorescence_raw: float


# ===========================================================================

def _parse_data_line(line: str) -> ProbeRecord | None:
    """Parse a 'DATA:entry,ms,temp,pressure,excitation,fluorescence' line."""
    try:
        _, csv = line.split("DATA:", 1)
        parts = csv.strip().split(",")
        if len(parts) != 6:
            return None
        return ProbeRecord(
            entry_num        = int(parts[0]),
            ms_since_start   = int(parts[1]),
            temperature_c    = float(parts[2]),
            pressure_dbar    = float(parts[3]),
            excitation_raw   = float(parts[4]),
            fluorescence_raw = float(parts[5]),
        )
    except (ValueError, IndexError):
        return None

# ===========================================================================

def _send_cmd(cmd: str) -> bool:
    """Write a CMD: line to the dongle. Returns False if not connected."""
    if _ser is None or not _ser.is_open:
        return False
    _ser.write((cmd + "\n").encode())
    return True

# ===========================================================================

async def connect_dongle(port: str | None = None) -> dict | None:
    """
    Try to open the Serial port and confirm the dongle is alive.

    If `port` is None, scans all COM ports for the first one that responds
    with DONGLE_READY_MSG within 2 s.

    Returns {"connected": True, "port": port} on success, None on failure.
    GUI's _find_dongle() retries every 3 s on None.
    """
    global _ser

    ports_to_try = [port] if port else [p.device for p in serial.tools.list_ports.comports()]

    for p in ports_to_try:
        try:
            candidate = serial.Serial(p, BAUD_RATE, timeout=2)
            # Drain any startup noise, look for ready message
            for _ in range(10):
                line = candidate.readline().decode("utf-8", errors="replace").strip()
                if DONGLE_READY_MSG in line:
                    _ser = candidate
                    logger.info("Dongle found on %s", p)
                    return {"connected": True, "port": p}
            candidate.close()
        except (serial.SerialException, OSError):
            continue

    return None

# ===========================================================================

def disconnect_dongle() -> None:
    """Close the serial port cleanly."""
    global _ser
    if _ser and _ser.is_open:
        _ser.close()
    _ser = None


# ===========================================================================
# Probe commands

def send_prepare_dive(date_str: str, time_str: str) -> bool:
    """Send CMD:PREPARE,<date>,<time> to the dongle."""
    return _send_cmd(f"CMD:PREPARE,{date_str},{time_str}")


def send_retrieve() -> bool:
    """Send CMD:RETRIEVE to trigger ESP-NOW data transfer."""
    return _send_cmd("CMD:RETRIEVE")


def send_status_request() -> bool:
    """Send CMD:STATUS to request battery level."""
    return _send_cmd("CMD:STATUS")


# ===========================================================================

def sync_probe(log_callback=None, on_record=None) -> list[dict]:
    """
    Reads DATA: lines from the already-open serial port until [SESSION] EOF.
    
    - log_callback(str)   : called for every non-DATA line ([SESSION], [ERROR], etc.)
    - on_record(dict)     : called live per record so GUI can update incrementally

    Returns list of record dicts (same keys as ProbeRecord fields).
    Raises RuntimeError if the serial port is not open.
    """
    if _ser is None or not _ser.is_open:
        raise RuntimeError("Serial port not open. Connect dongle first.")

    # Extend timeout for the duration of the transfer
    _ser.timeout = 30
    records: list[ProbeRecord] = []

    try:
        for raw_line in _ser:
            line = raw_line.decode("utf-8", errors="replace").strip()

            if line.startswith("DATA:"):
                rec = _parse_data_line(line)
                if rec:
                    records.append(rec)
                    if on_record:
                        on_record(asdict(rec))      # pass dict so GUI stays decoupled

            elif "[SESSION] EOF" in line:
                if log_callback:
                    log_callback(line)
                break

            elif line.startswith("BATT:"):
                # Battery update mid-session (optional — dongle can send this anytime)
                if log_callback:
                    log_callback(line)

            else:
                if log_callback:
                    log_callback(line)

    finally:
        _ser.timeout = 2    # restore normal timeout

    logger.info("Sync complete. %d records received.", len(records))
    return [asdict(r) for r in records]


# ===========================================================================
# Thread helpers for running blocking code without freezing the GUI

def run_async(coro, callback=None):
    """
    Run an async coroutine in a background thread without blocking the GUI.
    callback(result, error) is called from that thread when done.
    GUI handlers use self.after(0, ...) to push UI updates to the main thread.
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

    threading.Thread(target=_thread, daemon=True).start()


def run_in_thread(fn, callback=None):
    """
    Run a plain blocking function in a background thread without blocking the GUI.
    Use this for sync_probe() and any other blocking I/O.
    callback(result, error) is called from that thread when done.
    """
    def _thread():
        try:
            result = fn()
            if callback:
                callback(result, None)
        except Exception as exc:
            if callback:
                callback(None, exc)

    threading.Thread(target=_thread, daemon=True).start()