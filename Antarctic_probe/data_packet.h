/**
 * data_packet.h
 * =============
 * Defines the binary wire format shared between the ESP32-C6 packer and the
 * Python parser on the laptop. THIS IS THE CONTRACT — do not change field
 * order, byte widths, or endianness without updating packet_parser.py too,
 * and without telling Kiyuran (SS2).
 *
 * All multi-byte fields are LITTLE-ENDIAN (native to ESP32 and x86 Python).
 *
 * Packet layout (18 bytes total):
 * ┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
 * │  [0:2]   │  [2:4]   │  [4:6]   │  [6:8]   │  [8:10]  │ [10:12]  │ [12:14]  │ [14:16]  │ [16:18]  │
 * │ uint16   │ uint16   │ int16    │ int16    │ int16    │ int16    │ int16    │ int16    │ int16    │
 * │ sequence │ CRC-16   │ Δtimestamp│ Δtemp   │ Δdepth   │ Δsalin.  │ ΔdissO2  │ Δchloro  │ ΔpH      │
 * │          │ /CCITT   │ (ms)     │ (0.01°C) │ (0.01m)  │(0.001PSU)│(0.1µmol) │(0.001µg) │(0.001LSB)│
 * └──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘
 *
 * Delta encoding:
 *   - First record in a session: all delta fields carry absolute values
 *     (i.e. the actual reading, not a difference). The Python side treats
 *     "previous = zero baseline" for the first packet.
 *   - Every subsequent record: each field = (current_value - previous_value)
 *     expressed in the LSB units shown above.
 *   - This keeps most deltas small, compressing well into int16.
 *
 * CRC:
 *   - CRC-16/CCITT (poly 0x1021, init 0xFFFF)
 *   - Computed over bytes [0:2] + [4:18]  (i.e. everything EXCEPT [2:4])
 *   - Stored little-endian at bytes [2:4]
 */

#pragma once
#include <stdint.h>

// ── Packet size ────────────────────────────────────────────────────────────
// Update this constant (and packet_parser.py PACKET_SIZE) if the struct changes.
#define PACKET_SIZE_BYTES  18

// ── LSB scale factors ──────────────────────────────────────────────────────
// These define the resolution of each encoded field.
// e.g. a raw int16 value of 2350 for temperature means 23.50 °C.
#define LSB_TEMPERATURE    0.01f   // °C per LSB
#define LSB_DEPTH          0.01f   // m per LSB
#define LSB_SALINITY       0.001f  // PSU per LSB
#define LSB_DISSOLVED_O2   0.1f    // µmol/L per LSB
#define LSB_CHLOROPHYLL    0.001f  // µg/L per LSB
#define LSB_PH             0.001f  // pH units per LSB

// ── CRC constants ─────────────────────────────────────────────────────────
#define CRC_POLY  0x1021
#define CRC_INIT  0xFFFF

// ── Raw packet struct ──────────────────────────────────────────────────────
// __attribute__((packed)) prevents the compiler from adding padding bytes,
// which would break the 18-byte layout assumed by the Python side.
typedef struct __attribute__((packed)) {
    uint16_t sequence;       // Packet counter — monotonically increasing per session
    uint16_t crc;            // CRC-16/CCITT over (sequence + all delta fields)
    int16_t  delta_timestamp_ms;   // ms since last record (absolute on first packet)
    int16_t  delta_temperature;    // in units of LSB_TEMPERATURE
    int16_t  delta_depth;          // in units of LSB_DEPTH
    int16_t  delta_salinity;       // in units of LSB_SALINITY
    int16_t  delta_dissolved_o2;   // in units of LSB_DISSOLVED_O2
    int16_t  delta_chlorophyll;    // in units of LSB_CHLOROPHYLL
    int16_t  delta_ph;             // in units of LSB_PH
} SensorPacket_t;

// Sanity check — will cause a compile error if the struct ends up the wrong size.
// If you add or remove fields, update PACKET_SIZE_BYTES to match.
static_assert(sizeof(SensorPacket_t) == PACKET_SIZE_BYTES,
              "SensorPacket_t size mismatch — update PACKET_SIZE_BYTES");

// ── Previous-state tracking for delta encoding ────────────────────────────
// The packer needs to remember the last transmitted values to compute deltas.
// Store these in a single struct so they can be saved to MRAM across sessions
// if you want delta encoding to persist (optional — discuss with Kiyuran).
typedef struct {
    uint32_t timestamp_ms;
    int16_t  temperature_raw;   // stored in LSB units to avoid repeated float conversion
    int16_t  depth_raw;
    int16_t  salinity_raw;
    int16_t  dissolved_o2_raw;
    int16_t  chlorophyll_raw;
    int16_t  ph_raw;
} PreviousValues_t;

// ── CRC function declaration ───────────────────────────────────────────────
// Implemented in data_packet.cpp (or inline below if you prefer a header-only approach).
// Computes CRC-16/CCITT over `len` bytes of `data`.
uint16_t crc16_ccitt(const uint8_t* data, uint16_t len);

// ── Helper: build and CRC-stamp a packet ──────────────────────────────────
// Call this instead of manually filling SensorPacket_t — it handles CRC for you.
// `seq`  : current sequence number
// `prev` : pointer to previous values (pass a zeroed struct for the first packet)
// `ts_ms`, `temp_c`, `depth_m`, etc. : current sensor readings in real units
// Returns a fully populated, CRC-stamped SensorPacket_t ready to transmit.
SensorPacket_t build_packet(
    uint16_t         seq,
    PreviousValues_t* prev,   // updated in-place after each call
    uint32_t ts_ms,
    float    temp_c,
    float    depth_m,
    float    salinity_psu,
    float    dissolved_o2_umol,
    float    chlorophyll_ug,
    float    ph
);