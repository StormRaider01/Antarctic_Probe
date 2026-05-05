"""
packet_parser.py
Deserialise binary wire format, validate CRC-16/CCITT, delta-decode sensor fields.
Wire format must be agreed with Kiyuran (SS2) — struct layout below is a placeholder
until data_packet.h is finalised.

Expected packet layout (little-endian):
  [0:2]   uint16  sequence number
  [2:4]   uint16  CRC-16/CCITT  (over bytes [0:N-2])
  [4:6]   int16   delta timestamp (ms) — first record uses absolute epoch
  [6:8]   int16   delta temperature  (0.01 °C LSB)
  [8:10]  int16   delta depth        (0.01 m LSB)
  [10:12] int16   delta salinity     (0.001 PSU LSB)
  [12:14] int16   delta dissolved O2 (0.1 µmol/L LSB)
  [14:16] int16   delta chlorophyll  (0.001 µg/L LSB)
  [16:18] int16   delta pH           (0.001 LSB)
  Total: 18 bytes per packet
"""

import struct
import logging
from dataclasses import dataclass

logger = logging.getLogger(__name__)

PACKET_SIZE = 18    # bytes — update when wire format is locked with SS2
CRC_POLY    = 0x1021
CRC_INIT    = 0xFFFF


@dataclass
class SensorRecord:
    sequence:     int
    timestamp_ms: int
    temperature:  float   # °C
    depth:        float   # m
    salinity:     float   # PSU
    dissolved_o2: float   # µmol/L
    chlorophyll:  float   # µg/L
    ph:           float


def _crc16_ccitt(data: bytes) -> int:
    crc = CRC_INIT
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ CRC_POLY
            else:
                crc <<= 1
        crc &= 0xFFFF
    return crc


def validate_crc(packet: bytes) -> bool:
    """CRC covers all bytes except the 2-byte CRC field at [2:4]."""
    payload = packet[0:2] + packet[4:]
    expected = _crc16_ccitt(payload)
    received = struct.unpack_from("<H", packet, 2)[0]
    if expected != received:
        logger.warning("CRC mismatch: expected 0x%04X, got 0x%04X", expected, received)
        return False
    return True


def parse_packet(packet: bytes, prev: SensorRecord | None) -> SensorRecord | None:
    """
    Deserialise one packet. Applies delta decoding relative to prev record.
    Returns None on CRC failure or malformed data.
    """
    if len(packet) != PACKET_SIZE:
        logger.error("Unexpected packet size: %d (expected %d)", len(packet), PACKET_SIZE)
        return None

    if not validate_crc(packet):
        return None

    seq, _, d_ts, d_temp, d_depth, d_sal, d_o2, d_chl, d_ph = struct.unpack_from("<HHhhhhhhh", packet)

    # First record: deltas are absolute values
    base_ts   = prev.timestamp_ms if prev else 0
    base_temp = int(prev.temperature  * 100)  if prev else 0
    base_dep  = int(prev.depth        * 100)  if prev else 0
    base_sal  = int(prev.salinity     * 1000) if prev else 0
    base_o2   = int(prev.dissolved_o2 * 10)   if prev else 0
    base_chl  = int(prev.chlorophyll  * 1000) if prev else 0
    base_ph   = int(prev.ph           * 1000) if prev else 0

    return SensorRecord(
        sequence     = seq,
        timestamp_ms = base_ts   + d_ts,
        temperature  = (base_temp + d_temp) / 100.0,
        depth        = (base_dep  + d_depth) / 100.0,
        salinity     = (base_sal  + d_sal)  / 1000.0,
        dissolved_o2 = (base_o2   + d_o2)   / 10.0,
        chlorophyll  = (base_chl  + d_chl)  / 1000.0,
        ph           = (base_ph   + d_ph)   / 1000.0,
    )


def parse_all(packets: list[bytes]) -> list[SensorRecord]:
    """Parse an ordered list of raw packets into records, chaining delta decoding."""
    records = []
    prev = None
    for pkt in packets:
        record = parse_packet(pkt, prev)
        if record:
            records.append(record)
            prev = record
        else:
            logger.warning("Dropped packet (seq unknown due to parse failure)")
    logger.info("Parsed %d/%d packets successfully.", len(records), len(packets))
    return records