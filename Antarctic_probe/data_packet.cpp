/**
 * data_packet.cpp
 * 
 * Implements the CRC-16/CCITT function and the build_packet() helper declared in data_packet.h.
 */

#include "data_packet.h"
#include <string.h>   // memset

// CRC-16/CCITT
/**
 * Standard CRC-16/CCITT implementation.
 * Poly: 0x1021   Init: 0xFFFF   No input/output reflection.
 * Must produce identical results to the Python _crc16_ccitt() in packet_parser.py.
 */
uint16_t crc16_ccitt(const uint8_t* data, uint16_t len) {
    uint16_t crc = CRC_INIT;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ CRC_POLY;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// Internal: compute CRC over a packet (excluding the CRC field itself) 
/**
 * The CRC covers bytes [0:2] (sequence) and bytes [4:18] (all delta fields).
 * Bytes [2:4] (the CRC field itself) are excluded — same as Python side.
 */
static uint16_t packet_crc(const SensorPacket_t* pkt) {
    const uint8_t* raw = (const uint8_t*)pkt;

    // Compute CRC over the two separate regions separately, combining the result.
    // Region 1: bytes 0–1 (sequence)
    // Region 2: bytes 4–17 (all delta fields)
    //
    // We do this by running the CRC algorithm over region 1 first, then
    // continuing from the same CRC state into region 2.

    uint16_t crc = CRC_INIT;

    // Region 1: sequence (2 bytes)
    for (uint16_t i = 0; i < 2; i++) {
        crc ^= (uint16_t)raw[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ CRC_POLY : (crc << 1);
        }
    }

    // Region 2: delta fields (bytes 4–17, skipping CRC field at bytes 2–3)
    for (uint16_t i = 4; i < PACKET_SIZE_BYTES; i++) {
        crc ^= (uint16_t)raw[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ CRC_POLY : (crc << 1);
        }
    }

    return crc;
}

//  build_packet
/**
 * Converts real-unit sensor readings into a delta-encoded, CRC-stamped packet.
 * Also updates `prev` in-place so the next call computes correct deltas.
 *
 * For the very first packet, pass a zeroed PreviousValues_t:
 *   PreviousValues_t prev = {0};
 * The deltas will then equal the absolute readings (since 0 is the base),
 * which matches what packet_parser.py expects for the first record.
 */
SensorPacket_t build_packet(
    uint16_t          seq,
    PreviousValues_t* prev,
    uint32_t ts_ms,
    float    temp_c,
    float    depth_m,
    float    salinity_psu,
    float    dissolved_o2_umol,
    float    chlorophyll_ug,
    float    ph
) {
    // Convert float readings to raw integer units (same LSB scale as the struct)
    int16_t ts_raw   = (int16_t)(ts_ms);                              // ms — note: wraps at 32.7s; see note below *
    int16_t temp_raw = (int16_t)(temp_c          / LSB_TEMPERATURE);
    int16_t dep_raw  = (int16_t)(depth_m         / LSB_DEPTH);
    int16_t sal_raw  = (int16_t)(salinity_psu    / LSB_SALINITY);
    int16_t o2_raw   = (int16_t)(dissolved_o2_umol / LSB_DISSOLVED_O2);
    int16_t chl_raw  = (int16_t)(chlorophyll_ug  / LSB_CHLOROPHYLL);
    int16_t ph_raw   = (int16_t)(ph              / LSB_PH);

    // * Timestamp note: int16_t overflows at ±32767 ms (~32 seconds).
    //   If your sampling interval is longer (e.g. every few minutes),
    //   change delta_timestamp_ms to int32_t in the struct AND update
    //   packet_parser.py's struct.unpack format string and PACKET_SIZE.

    // Compute deltas relative to previous record
    SensorPacket_t pkt;
    pkt.sequence             = seq;
    pkt.delta_timestamp_ms   = (int16_t)(ts_raw  - (int16_t)prev->timestamp_ms);
    pkt.delta_temperature    = temp_raw - prev->temperature_raw;
    pkt.delta_depth          = dep_raw  - prev->depth_raw;
    pkt.delta_salinity       = sal_raw  - prev->salinity_raw;
    pkt.delta_dissolved_o2   = o2_raw   - prev->dissolved_o2_raw;
    pkt.delta_chlorophyll    = chl_raw  - prev->chlorophyll_raw;
    pkt.delta_ph             = ph_raw   - prev->ph_raw;

    // Stamp the CRC (must be done after all delta fields are filled)
    pkt.crc = packet_crc(&pkt);

    // Update previous values for next call
    prev->timestamp_ms      = ts_ms;
    prev->temperature_raw   = temp_raw;
    prev->depth_raw         = dep_raw;
    prev->salinity_raw      = sal_raw;
    prev->dissolved_o2_raw  = o2_raw;
    prev->chlorophyll_raw   = chl_raw;
    prev->ph_raw            = ph_raw;

    return pkt;
}