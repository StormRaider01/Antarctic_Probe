"""
test_pipeline.py
================
Hardware-free end-to-end test of the Backend data pipeline.

Replaces the physical dongle with an in-memory fake that follows the same
serial protocol, then runs each Backend function in sequence and checks
every record field.

Run from the GUI/ directory:
    python test_pipeline.py
"""

import sys
import os
import queue
import threading
import time

sys.path.insert(0, os.path.dirname(__file__))
import Backend as bk   # noqa: E402

import io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

PASS = "[PASS]"
FAIL = "[FAIL]"
INFO = "[INFO]"


# ===========================================================================
# Fake probe records that the dongle "received" and will stream
# Format mirrors exactly what the probe's log_sensor_data() writes to FRAM,
# then build_record() and the dongle's DATA: print produce.
# ===========================================================================
FAKE_RECORDS = [
    # (entry, ms_since_start, temp,   pressure, spec1–spec11)
    (0,  0,     1400.0, 2200.0, [100.0, 105.0,  90.0, 110.0,  95.0, 100.0, 120.0,  80.0,  90.0, 110.0, 105.0]),
    (1,  5000,  1380.5, 2210.3, [ 98.0, 103.0,  88.0, 108.0,  93.0,  98.0, 118.0,  78.0,  88.0, 108.0, 103.0]),
    (2, 10000,  1360.2, 2220.7, [ 96.0, 101.0,  86.0, 106.0,  91.0,  96.0, 116.0,  76.0,  86.0, 106.0, 101.0]),
    (3, 15000,  1340.8, 2230.1, [ 94.0,  99.0,  84.0, 104.0,  89.0,  94.0, 114.0,  74.0,  84.0, 104.0,  99.0]),
    (4, 20000,  1320.3, 2240.5, [ 92.0,  97.0,  82.0, 102.0,  87.0,  92.0, 112.0,  72.0,  82.0, 102.0,  97.0]),
]


def _format_data_line(entry, ms, temp, pressure, specs):
    """Mirrors TestDongle.ino OnDataRecv DATA: print with 4 decimal places."""
    parts = [str(entry), str(ms), f"{temp:.4f}", f"{pressure:.4f}"]
    parts += [f"{s:.4f}" for s in specs]
    return "DATA:" + ",".join(parts)


# ===========================================================================
# FakeDongleSerial — bidirectional in-memory serial port
# ===========================================================================

class FakeDongleSerial:
    """
    Wraps two queues to mimic pyserial's Serial interface.

    Backend.py calls  write()        → command goes into _to_dongle queue
    Backend.py calls  readline()     → response comes from _to_backend queue
    A background thread runs the dongle state machine and fills _to_backend.
    """

    def __init__(self, records):
        self._to_backend = queue.Queue()   # dongle → python reads
        self._to_dongle  = queue.Queue()   # python writes → dongle reads
        self._records    = records
        self.timeout     = 5
        self.is_open     = True

        self._thread = threading.Thread(target=self._dongle_loop, daemon=True)
        self._thread.start()

    # ── pyserial-compatible API ────────────────────────────────────────────

    def write(self, data: bytes) -> int:
        self._to_dongle.put(data)
        return len(data)

    def readline(self) -> bytes:
        try:
            return self._to_backend.get(timeout=self.timeout)
        except queue.Empty:
            return b""

    @property
    def in_waiting(self) -> int:
        return self._to_backend.qsize()

    def close(self):
        self.is_open = False

    def __iter__(self):
        return self

    def __next__(self) -> bytes:
        line = self.readline()
        if not line:
            raise StopIteration
        return line

    # ── Dongle state machine ───────────────────────────────────────────────

    def _send(self, msg: str):
        """Queue a line for Backend to read."""
        self._to_backend.put((msg + "\n").encode())

    def _recv(self, timeout: float = 10.0) -> str:
        """Block until Backend writes a command."""
        try:
            data = self._to_dongle.get(timeout=timeout)
            return data.decode("utf-8", errors="replace").strip()
        except queue.Empty:
            return ""

    def _dongle_loop(self):
        """
        Simulates the dongle's serial protocol in the correct order:
          1. Heartbeat (boot)
          2. STOPHEARTBEAT
          3. CONNECT  → ACK:CONNECT,BATT:70
          4. RETRIEVE → ACK:RETRIEVE, HEADER, DATA lines, EOF
        """
        # Boot heartbeat — Backend.connect_dongle() looks for this
        self._send("[HBT] Waiting for GUI connection...")

        while True:
            cmd = self._recv()
            if not cmd:
                break

            print(f"  {INFO} Fake dongle received: {repr(cmd)}")

            if "[CMD]:STOPHEARTBEAT" in cmd:
                self._send("Heartbeat Stopped. Ready to connect to probe.")

            elif "[CMD]:CONNECT" in cmd:
                time.sleep(0.05)
                self._send("Attempting to connect to probe...")
                time.sleep(0.05)
                self._send("[ACK]:CONNECT,BATT:70")

            elif "[CMD]:PREPARE" in cmd:
                # Parse date/time from command
                self._send("[DEBUG]: The date received is 2026-05-15")
                self._send("[DEBUG]: The time received is 12:00:00")
                self._send("[INFO]: Command Prepare forwarded successfully.")
                time.sleep(0.05)
                self._send("[ACK]:PREPARE")

            elif "[CMD]:RETRIEVE" in cmd:
                self._send("Command Retrieve forwarded.")
                time.sleep(0.05)
                self._send("[ACK]:RETRIEVE")
                time.sleep(0.05)
                # ── stream data ──────────────────────────────────────────
                self._send(f"[SESSION] HEADER records={len(self._records)} date=20260515")
                for (entry, ms, temp, pressure, specs) in self._records:
                    self._send(_format_data_line(entry, ms, temp, pressure, specs))
                    time.sleep(0.01)
                self._send("[SESSION] EOF")
                break

            elif "[CMD]:DISCONNECT" in cmd:
                self._send("Command Disconnect forwarded.")
                break


# ===========================================================================
# Test helpers
# ===========================================================================

_pass_count = 0
_fail_count = 0


def check(desc: str, condition: bool, detail: str = ""):
    global _pass_count, _fail_count
    if condition:
        _pass_count += 1
        print(f"  {PASS} {desc}")
    else:
        _fail_count += 1
        msg = f"  {FAIL} {desc}"
        if detail:
            msg += f"\n         → {detail}"
        print(msg)


def section(title: str):
    print(f"\n{'-' * 60}")
    print(f"  {title}")
    print(f"{'-' * 60}")


# ===========================================================================
# Tests
# ===========================================================================

def test_parse_data_line():
    section("1 · _parse_data_line  (unit test, no serial)")

    good = "DATA:3,15000,1340.8000,2230.1000,94.0000,99.0000,84.0000,104.0000,89.0000,94.0000,114.0000,74.0000,84.0000,104.0000,99.0000"
    r = bk._parse_data_line(good)
    check("Valid 15-field line returns a ProbeRecord", r is not None)
    if r:
        check("entry_num parsed",      r.entry_num      == 3)
        check("ms_since_start parsed", r.ms_since_start == 15000)
        check("temperature_c parsed",  abs(r.temperature_c - 1340.8) < 0.01)
        check("pressure_dbar parsed",  abs(r.pressure_dbar - 2230.1) < 0.01)
        check("spec6 (excitation)",    abs(r.spec6 - 94.0) < 0.01)
        check("spec7 (fluorescence)",  abs(r.spec7 - 114.0) < 0.01)
        check("spec11 parsed",         abs(r.spec11 - 99.0) < 0.01)

    bad_cases = [
        ("Too few fields",   "DATA:1,2,3,4,5"),
        ("Too many fields",  "DATA:" + ",".join(["1"] * 16)),
        ("Non-numeric",      "DATA:1,2,abc,4,5,6,7,8,9,10,11,12,13,14,15"),
        ("No DATA prefix",   "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15"),
        ("Empty string",     ""),
    ]
    for desc, line in bad_cases:
        r = bk._parse_data_line(line)
        check(f"Rejects malformed ({desc})", r is None,
              f"got {r!r} instead of None")


def test_connect_probe(fake_ser: FakeDongleSerial):
    section("2 · connect_probe  (CMD:CONNECT → ACK + battery)")

    bk._ser = fake_ser

    result = bk.connect_probe()

    check("Returns a dict",                isinstance(result, dict))
    check("probe_connected is True",       result.get("probe_connected") is True)
    check("battery_pct extracted (70)",    result.get("battery_pct") == 70,
          f"got {result.get('battery_pct')!r}")
    check("bk.is_probe_connected is True", bk.is_probe_connected is True)


def test_send_retrieve(fake_ser: FakeDongleSerial):
    section("3 · send_retrieve  (CMD:RETRIEVE → ACK)")

    bk._ser = fake_ser

    result = bk.send_retrieve()
    check("Returns True (ACK:RETRIEVE seen)", result is True,
          f"got {result!r}")


def test_sync_probe(fake_ser: FakeDongleSerial):
    section("4 · sync_probe  (DATA lines → mapped dicts)")

    bk._ser = fake_ser

    log_lines  = []
    live_recs  = []

    records = bk.sync_probe(
        log_callback=lambda msg: log_lines.append(msg),
        on_record   =lambda r:  live_recs.append(r),
    )

    check(f"Correct record count ({len(FAKE_RECORDS)})",
          len(records) == len(FAKE_RECORDS),
          f"got {len(records)}")

    check("on_record fired for each record",
          len(live_recs) == len(FAKE_RECORDS),
          f"got {len(live_recs)}")

    check("EOF seen in log", any("[SESSION] EOF" in l for l in log_lines),
          f"log_lines = {log_lines}")

    # Verify every field on every record
    expected_keys = {
        "sequence", "timestamp_ms", "temperature", "pressure",
        "spec1", "spec2", "spec3", "spec4", "spec5",
        "spec6", "spec7", "spec8", "spec9", "spec10", "spec11",
        "is_anomaly",
    }

    all_fields_ok = True
    for i, (entry, ms, temp, pressure, specs) in enumerate(FAKE_RECORDS):
        if i >= len(records):
            break
        r = records[i]

        missing = expected_keys - r.keys()
        if missing:
            check(f"Record {i} has all expected keys", False,
                  f"missing: {missing}")
            all_fields_ok = False
            continue

        field_checks = [
            ("sequence",     r["sequence"],              entry),
            ("timestamp_ms", r["timestamp_ms"],          ms),
            ("temperature",  round(r["temperature"], 1), round(temp, 1)),
            ("pressure",     round(r["pressure"],    1), round(pressure, 1)),
        ]
        for j, s in enumerate(specs, start=1):
            field_checks.append(
                (f"spec{j}", round(r[f"spec{j}"], 1), round(s, 1))
            )

        for field, got, want in field_checks:
            if got != want:
                check(f"Record {i} field '{field}'", False,
                      f"got {got}, want {want}")
                all_fields_ok = False

    if all_fields_ok:
        check("All record fields correct across all records", True)

    # Spot-check the live records match the return value
    if records and live_recs:
        check("on_record dict matches return dict for record 0",
              live_recs[0] == records[0])


# ===========================================================================
# Main
# ===========================================================================

def main():
    print()
    print("=" * 60)
    print("  Antarctic Probe — Backend Pipeline Test")
    print("  (no hardware or COM ports required)")
    print("=" * 60)

    # Test 1: no serial needed
    test_parse_data_line()

    # Tests 2–4: need the fake serial.
    # One FakeDongleSerial handles the full session in order:
    #   connect_probe → send_retrieve → sync_probe
    fake_ser = FakeDongleSerial(FAKE_RECORDS)

    test_connect_probe(fake_ser)
    test_send_retrieve(fake_ser)
    test_sync_probe(fake_ser)

    # Summary
    total = _pass_count + _fail_count
    print(f"\n{'=' * 60}")
    print(f"  Result: {_pass_count}/{total} checks passed", end="")
    if _fail_count == 0:
        print("  All good.")
    else:
        print(f"  {_fail_count} failure(s) -- see above.")
    print("=" * 60)
    print()

    return _fail_count


if __name__ == "__main__":
    sys.exit(main())
