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
import time


# ===========================================================================
# Global variables and config
is_probe_connected = None
battery_pct = None

logger = logging.getLogger(__name__)

_ser: serial.Serial | None = None   # shared Serial instance, owned by Backend

BAUD_RATE        = 115200
DONGLE_READY_MSG = "[HBT] Waiting for GUI connection..."          # must match what receiver_dongle.ino prints on boot
PROBE_CONNECTED_MSG   = "[ACK]:CONNECT"  # must match .ino after CMD:CONNECT

@dataclass
class ProbeRecord:
    entry_num:        int
    ms_since_start:   int
    temperature_c:    float
    pressure_dbar:    float
    spec1:            float
    spec2:            float
    spec3:            float
    spec4:            float
    spec5:            float
    spec6:            float  # Excitation
    spec7:            float  # Fluorescence
    spec8:            float
    spec9:            float
    spec10:           float
    spec11:           float
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
            spec1            = float(parts[4]),
            spec2            = float(parts[5]),
            spec3            = float(parts[6]),
            spec4            = float(parts[7]),
            spec5            = float(parts[8]),
            spec6            = float(parts[9]),
            spec7            = float(parts[10]),
            spec8            = float(parts[11]),
            spec9            = float(parts[12]),
            spec10           = float(parts[13]),
            spec11           = float(parts[14]),
        )
    except (ValueError, IndexError):
        return None

# ===========================================================================

import time

def wait_for_string(ser, targetString, timeout=10):
    global battery_pct
    start_time = time.time()
    
    # Set a short timeout on the serial port itself if not already set
    # ser.timeout = 0.1 

    while (time.time() - start_time) < timeout:
        if ser.in_waiting > 0:
            # Read and clean the line
            line_raw = ser.readline()
            try:
                line = line_raw.decode("utf-8", errors="replace").strip()
            except Exception:
                continue

            if line:
                print(f"[DEBUG WAIT]: {repr(line)}") # repr() helps catch hidden chars

                # Handle Battery Update
                if "BATT:" in line:
                    try:
                        # Split by colon and take the last part
                        # Before — broke on "70%"
                        parts = line.split(":")
                        battery_pct = int(parts[-1].strip())

                        # After — handles "[ACK]:CONNECT,BATT:70"
                        # batt_str = line.split("BATT:")[1].split(",")[0].strip()
                        # battery_pct = int(batt_str)
                    except ValueError:
                        pass

                # Handle Target String
                if targetString in line:
                    return True
        
        time.sleep(0.05) # Small sleep to prevent CPU spiking
    
    return False


# ===========================================================================

async def connect_dongle(port: str | None = None) -> dict | None:
    """
    Try to open the Serial port and confirm the dongle is alive.

    If `port` is None, scans all COM ports for the first one that responds
    with DONGLE_READY_MSG within 2 s.

    Returns {"connected": True, "port": port} on success, None on failure.
    GUI's _find_dongle() retries every 3 s on None.
    """
    global _ser     # Global declared so that the _ser variable can be updated

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
                    print(f"[DEBUG] Read: {line}")
                
                if DONGLE_READY_MSG in line:
                    _ser = candidate
                    print(f"[INFO]: Dongle found on {p}")

                    # Tell dongle to stop heartbeat and enter READY state
                    # This is to prevent heartbeat messages from interfering with our command/response flow.
                    candidate.write(b"[CMD]:STOPHEARTBEAT")

                    return {"connected": True, "port": p}
            
            candidate.close()
        except Exception as e:
            print(f"[DEBUG] Port error: {e} on {p}")
            continue

    return None


# ===========================================================================


def connect_probe():# -> dict | None:
    """
    Send CMD:CONNECT to tell the dongle to initialise ESP-NOW and register
    the probe as a peer. Blocks until the dongle confirms or times out.
 
    Returns {"probe_connected": is_probe_connected, "battery_pct": battery_pct} on success, None on failure.
    Run via run_in_thread() so the GUI doesn't freeze.
    """
    global is_probe_connected

    if _ser is None or not _ser.is_open:
        raise RuntimeError("Serial port not open. Dongle not found yet.")
 
    _ser.write(b"[CMD]:CONNECT")

    is_probe_connected = wait_for_string(_ser, targetString="[ACK]:CONNECT")
            
    return {"probe_connected": is_probe_connected, "battery_pct": battery_pct}


# ===========================================================================

def disconnect_probe() -> None:
    """De-init ESP NOW"""
    global is_probe_connected

    # Send command
    _ser.write(b"[CMD]:DISCONNECT")

    # is_probe_connected = not wait_for_string(_ser, targetString="[ACK]:DISCONNECT")
    is_probe_connected = False

    return not is_probe_connected   # if is_probe_connected = False then return True

# ===========================================================================
# Probe commands

def send_prepare_dive(date_str: str, time_str: str) -> bool:
    """Send CMD:PREPARE,<date>,<time> to the dongle."""

    commandString = "[CMD]:PREPARE," + date_str + "," + time_str
    _ser.write(commandString.encode())

    yes = wait_for_string(_ser, targetString="[ACK]:PREPARE")  # wait for confirmation
    wait_for_string(_ser, targetString="[DEBUG]: The date ")
    wait_for_string(_ser, targetString="[DEBUG]: The time ")

    return yes


def send_retrieve() -> bool:
    """Send CMD:RETRIEVE to trigger ESP-NOW data transfer."""

    _ser.write(b"[CMD]:RETRIEVE")
    wait_for_string(_ser, targetString="[ACK]:RETRIEVE")



# ===========================================================================


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
                        "spec1":        rec.spec1,
                        "spec2":        rec.spec2,
                        "spec3":        rec.spec3,
                        "spec4":        rec.spec4,
                        "spec5":        rec.spec5,
                        "spec6":        rec.spec6,
                        "spec7":        rec.spec7,
                        "spec8":        rec.spec8,
                        "spec9":        rec.spec9,
                        "spec10":       rec.spec10,
                        "spec11":       rec.spec11,
                        "is_anomaly":   rec.is_anomaly
                    }
                    
                    if on_record:
                        on_record(mapped_rec)      # pass dict so GUI stays decoupled

            elif "[SESSION] EOF" in line:
                if log_callback:
                    log_callback(line)
                break

            elif line.startswith("[BAT]:"):
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
                    "spec1":        rec.spec1,
                    "spec2":        rec.spec2,
                    "spec3":        rec.spec3,
                    "spec4":        rec.spec4,
                    "spec5":        rec.spec5,
                    "spec6":        rec.spec6,
                    "spec7":        rec.spec7,
                    "spec8":        rec.spec8,
                    "spec9":        rec.spec9,
                    "spec10":       rec.spec10,
                    "spec11":       rec.spec11,
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
    

