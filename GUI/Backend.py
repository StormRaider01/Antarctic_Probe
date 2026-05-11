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
DONGLE_READY_MSG = "Ready. Waiting for probe..."          # must match what receiver_dongle.ino prints on boot

@dataclass
class ProbeRecord:
    entry_num:        int
    ms_since_start:   int
    temperature_c:    float
    pressure_dbar:    float
    excitation_raw:   float
    fluorescence_raw: float
    is_anomaly:       bool = False


# ===========================================================================

def _parse_data_line(line: str) -> ProbeRecord | None:
    """Parse a 'DATA:entry,ms,temp,pressure,spec1...spec11' line."""
    try:
        _, csv = line.split("DATA:", 1)
        parts = csv.strip().split(",")
        if len(parts) != 15:
            return None
        return ProbeRecord(
            entry_num        = int(parts[0]),
            ms_since_start   = int(parts[1]),
            temperature_c    = float(parts[2]),
            pressure_dbar    = float(parts[3]),
            excitation_raw   = float(parts[9]),  # spec6 (0-indexed 5, but after 4 fields it's 4+5=9)
            fluorescence_raw = float(parts[10]), # spec7 (4+6=10)
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
            # For Native USB (ESP32-C6), DTR must be True for the board to send data
            candidate = serial.Serial(p, BAUD_RATE, timeout=1)
            candidate.dtr = True
            candidate.rts = True

            # Drain any startup noise, look for ready message
            for _ in range(5):
                raw = candidate.readline()
                line = raw.decode("utf-8", errors="replace").strip()
                
                # Print everything Python reads so we aren't flying blind
                if raw:
                    print(f"[DEBUG - {p}] Read: {line}")
                
                if DONGLE_READY_MSG in line:
                    _ser = candidate
                    logger.info("Dongle found on %s", p)
                    return {"connected": True, "port": p}
            
            candidate.close()
        except Exception as e:
            print(f"[DEBUG - {p}] Port error: {e}")
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


# Initialize the ML Detector dynamically
try:
    import sys
    import os
    sys.path.append(os.path.join(os.path.dirname(__file__), 'ml_backend'))
    from anomaly_detector import MarineAnomalyDetector
    _detector = MarineAnomalyDetector()
except Exception as e:
    logger.error("Failed to load MarineAnomalyDetector: %s", e)
    _detector = None

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
                # Evaluate anomaly first
                _, csv_payload = line.split("DATA:", 1)
                is_anomaly = False
                if _detector:
                    is_anomaly = _detector.evaluate_reading(csv_payload.strip())
                
                rec = _parse_data_line(line)
                if rec:
                    rec.is_anomaly = is_anomaly
                    records.append(rec)
                    
                    mapped_rec = {
                        "sequence":     rec.entry_num,
                        "timestamp_ms": rec.ms_since_start,
                        "temperature":  rec.temperature_c,
                        "pressure":     rec.pressure_dbar,
                        "excitation":   rec.excitation_raw,
                        "fluorescence": rec.fluorescence_raw,
                        "is_anomaly":   rec.is_anomaly
                    }
                    
                    if on_record:
                        on_record(mapped_rec)      # pass dict so GUI stays decoupled

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
    
    # Map all records before returning
    mapped_records = []
    for r in records:
        mapped_records.append({
            "sequence":     r.entry_num,
            "timestamp_ms": r.ms_since_start,
            "temperature":  r.temperature_c,
            "pressure":     r.pressure_dbar,
            "excitation":   r.excitation_raw,
            "fluorescence": r.fluorescence_raw,
            "is_anomaly":   r.is_anomaly
        })
    return mapped_records

import threading

_sim_paused = threading.Event()
_sim_stopped = threading.Event()

def pause_simulation():
    _sim_paused.set()

def resume_simulation():
    _sim_paused.clear()

def stop_simulation():
    _sim_stopped.set()
    _sim_paused.clear() # Unblock if currently paused

def simulate_incoming_data(log_callback=None, on_record=None) -> list[dict]:
    """
    Simulates incoming data by reading dummy_marine_data.csv line by line.
    Passes data exactly like sync_probe, 1 row every 0.5s.
    """
    import time
    
    csv_path = os.path.join(os.path.dirname(__file__), 'ml_backend', 'dummy_marine_data.csv')
    if not os.path.exists(csv_path):
        if log_callback:
            log_callback(f"[ERROR] Dummy data file not found at {csv_path}")
        return []

    _sim_stopped.clear()
    _sim_paused.clear()

    if log_callback:
        log_callback("[SESSION] Simulated transfer started.")

    records: list[ProbeRecord] = []

    with open(csv_path, 'r') as f:
        next(f) # Skip header
        for line in f:
            if _sim_stopped.is_set():
                if log_callback:
                    log_callback("[SESSION] Simulation stopped by user.")
                break

            while _sim_paused.is_set() and not _sim_stopped.is_set():
                time.sleep(0.1)
                
            if _sim_stopped.is_set():
                if log_callback:
                    log_callback("[SESSION] Simulation stopped by user.")
                break

            line = line.strip()
            if not line:
                continue

            data_str = f"DATA:{line}"
            
            # Evaluate anomaly first
            is_anomaly = False
            if _detector:
                is_anomaly = _detector.evaluate_reading(line)
            
            rec = _parse_data_line(data_str)
            if rec:
                rec.is_anomaly = is_anomaly
                records.append(rec)
                
                mapped_rec = {
                    "sequence":     rec.entry_num,
                    "timestamp_ms": rec.ms_since_start,
                    "temperature":  rec.temperature_c,
                    "pressure":     rec.pressure_dbar,
                    "excitation":   rec.excitation_raw,
                    "fluorescence": rec.fluorescence_raw,
                    "is_anomaly":   rec.is_anomaly
                }
                
                if on_record:
                    on_record(mapped_rec)
            
            # Sleep 0.5s but allow fast interruption
            for _ in range(5):
                if _sim_stopped.is_set():
                    break
                time.sleep(0.1)

    if not _sim_stopped.is_set() and log_callback:
        log_callback("[SESSION] EOF")

    logger.info("Simulation complete. %d records processed.", len(records))

    mapped_records = []
    for r in records:
        mapped_records.append({
            "sequence":     r.entry_num,
            "timestamp_ms": r.ms_since_start,
            "temperature":  r.temperature_c,
            "pressure":     r.pressure_dbar,
            "excitation":   r.excitation_raw,
            "fluorescence": r.fluorescence_raw,
            "is_anomaly":   r.is_anomaly
        })
    return mapped_records

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


async def mock_get_status() -> dict:
    """Mock status request since the real probe isn't hooked up yet."""
    import asyncio
    await asyncio.sleep(0.5)
    return {"battery_pct": 100}