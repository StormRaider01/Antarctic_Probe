"""
data_store.py
Persist parsed SensorRecords to SQLite (default) or CSV.
Joshua's GUI reads from the SQLite DB — do not change the schema without coordinating.
"""

import csv
import logging
import sqlite3
from pathlib import Path
from packet_parser import SensorRecord

logger = logging.getLogger(__name__)

DEFAULT_DB_PATH  = Path("probe_data.db")
DEFAULT_CSV_PATH = Path("probe_data.csv")

CREATE_TABLE_SQL = """
CREATE TABLE IF NOT EXISTS sensor_records (
    sequence     INTEGER PRIMARY KEY,
    timestamp_ms INTEGER NOT NULL,
    temperature  REAL,
    depth        REAL,
    salinity     REAL,
    dissolved_o2 REAL,
    chlorophyll  REAL,
    ph           REAL
);
"""


def init_db(db_path: Path = DEFAULT_DB_PATH) -> sqlite3.Connection:
    conn = sqlite3.connect(db_path)
    conn.execute(CREATE_TABLE_SQL)
    conn.commit()
    logger.info("SQLite DB ready at %s", db_path)
    return conn


def save_records_sqlite(
    records: list[SensorRecord],
    conn: sqlite3.Connection,
) -> int:
    """Insert records, skipping duplicates by sequence number. Returns count inserted."""
    rows = [
        (r.sequence, r.timestamp_ms, r.temperature, r.depth,
         r.salinity, r.dissolved_o2, r.chlorophyll, r.ph)
        for r in records
    ]
    cursor = conn.executemany(
        "INSERT OR IGNORE INTO sensor_records VALUES (?,?,?,?,?,?,?,?)", rows
    )
    conn.commit()
    logger.info("Inserted %d new records into SQLite.", cursor.rowcount)
    return cursor.rowcount


def save_records_csv(
    records: list[SensorRecord],
    csv_path: Path = DEFAULT_CSV_PATH,
    append: bool = True,
) -> None:
    mode = "a" if append else "w"
    write_header = not csv_path.exists() or not append
    with open(csv_path, mode, newline="") as f:
        writer = csv.writer(f)
        if write_header:
            writer.writerow([
                "sequence", "timestamp_ms", "temperature_C", "depth_m",
                "salinity_PSU", "dissolved_o2_umolL", "chlorophyll_ugL", "ph"
            ])
        for r in records:
            writer.writerow([
                r.sequence, r.timestamp_ms, r.temperature, r.depth,
                r.salinity, r.dissolved_o2, r.chlorophyll, r.ph
            ])
    logger.info("Saved %d records to CSV at %s", len(records), csv_path)