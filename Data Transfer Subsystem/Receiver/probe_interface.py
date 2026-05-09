"""
probe_interface.py
==================
Public API for Joshua's GUI. Reads CSV records from the receiver dongle
over USB Serial and stores them to SQLite.

The receiver dongle outputs lines like:
    DATA:0,0,2.50,10.00,512.00,128.00
    DATA:1,5000,2.55,11.00,512.00,130.50
    [SESSION] ...
    [ERROR]   ...

This module filters for DATA: lines, parses them, and stores them.
All other lines are available via the log_callback for display in a GUI pane.
"""

import serial
import sqlite3
import logging
from pathlib import Path
from dataclasses import dataclass, asdict

logger = logging.getLogger(__name__)

DEFAULT_PORT    = "COM3"         # Windows — change to /dev/ttyUSB0 on Linux/Mac
DEFAULT_BAUD    = 115200
DEFAULT_DB_PATH = Path("probe_data.db")

CREATE_TABLE_SQL = """
CREATE TABLE IF NOT EXISTS probe_records (
    entry_num        INTEGER PRIMARY KEY,
    ms_since_start   INTEGER NOT NULL,
    temperature_c    REAL,
    pressure_dbar    REAL,
    excitation_raw   REAL,
    fluorescence_raw REAL
);
"""


@dataclass
class ProbeRecord:
    entry_num:        int
    ms_since_start:   int
    temperature_c:    float
    pressure_dbar:    float
    excitation_raw:   float
    fluorescence_raw: float


def init_db(db_path: Path = DEFAULT_DB_PATH) -> sqlite3.Connection:
    conn = sqlite3.connect(db_path)
    conn.execute(CREATE_TABLE_SQL)
    conn.commit()
    return conn


def _parse_data_line(line: str) -> ProbeRecord | None:
    """Parse a 'DATA:...' line from the receiver dongle."""
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


def sync_probe(
    port: str = DEFAULT_PORT,
    db_path: Path = DEFAULT_DB_PATH,
    log_callback=None,          # optional fn(str) — called with non-DATA lines
    on_record=None,             # optional fn(ProbeRecord) — called live per record
) -> list[dict]:
    """
    Open the Serial port, read until EOF marker is seen, persist records.
    Returns list of record dicts for the GUI.

    `log_callback`  — GUI can pass a function to display [SESSION]/[ERROR] lines.
    `on_record`     — GUI can pass a function to update a live chart per record.
    """
    conn  = init_db(db_path)
    records: list[ProbeRecord] = []

    with serial.Serial(port, DEFAULT_BAUD, timeout=30) as ser:
        logger.info("Serial port %s open. Waiting for probe...", port)

        for raw_line in ser:
            line = raw_line.decode("utf-8", errors="replace").strip()

            if line.startswith("DATA:"):
                rec = _parse_data_line(line)
                if rec:
                    records.append(rec)
                    conn.execute(
                        "INSERT OR IGNORE INTO probe_records VALUES (?,?,?,?,?,?)",
                        (rec.entry_num, rec.ms_since_start, rec.temperature_c,
                         rec.pressure_dbar, rec.excitation_raw, rec.fluorescence_raw)
                    )
                    conn.commit()
                    if on_record:
                        on_record(rec)

            elif "[SESSION] EOF" in line:
                if log_callback: log_callback(line)
                break   # transfer complete

            else:
                if log_callback: log_callback(line)

    conn.close()
    logger.info("Sync complete. %d records received.", len(records))
    return [asdict(r) for r in records]


# ── Convenience wrapper for non-async callers ──────────────────────────────
def run_sync(port: str = DEFAULT_PORT, db_path: Path = DEFAULT_DB_PATH) -> list[dict]:
    return sync_probe(port=port, db_path=db_path)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    records = run_sync()
    print(f"Synced {len(records)} records.")
    if records:
        print("First:", records[0])
        print("Last: ", records[-1])
