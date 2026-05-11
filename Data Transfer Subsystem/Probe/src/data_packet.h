/**
 * data_packet.h
 * =============
 * Binary wire format shared between the ESP32-C6 probe (sender) and the
 * receiver dongle ESP32 (which forwards to the laptop Python GUI).
 *
 * All multi-byte fields are LITTLE-ENDIAN (native to ESP32 and x86).
 *
 * SESSION HEADER (sent once at start of transfer, 12 bytes):
 *   Lets the receiver know how many records to expect and when the session started.
 *
 * PROBE RECORD (25 bytes per measurement):
 *   ┌───────────┬───────────┬───────────┬───────────┬───────────┬───────────┬────────┐
 *   │  [0:4]    │  [4:8]    │  [8:12]   │ [12:16]   │ [16:20]   │ [20:24]   │ [24]   │
 *   │ uint32    │ uint32    │ float32   │ float32   │ float32   │ float32   │ uint8  │
 *   │ entry_num │ ms_since  │ temp      │ pressure  │ excitation│fluoresce- │ XOR    │
 *   │           │ _start    │ (°C)      │ (dbar)    │ (raw ADC) │ nce(rawAD)│checksum│
 *   └───────────┴───────────┴───────────┴───────────┴───────────┴───────────┴────────┘
 *
 * Checksum: XOR of all 24 preceding bytes. Receiver rejects packet if mismatch.
 *
 * Chlorophyll-A is NOT computed onboard. The laptop derives it from:
 *   excitation_raw and fluorescence_raw post-hoc (SS2's ML processing step).
 *
 * ESP-NOW MTU = 250 bytes. One record (25 bytes) fits easily.
 * For bulk transfer, up to 10 records can be batched in one ESP-NOW frame
 * using a BULK_PAYLOAD_t — but single-record sends are simpler and fine
 * given FRAM read speed and the short transfer window.
 */

#pragma once
#include <stdint.h>


// =============================================================================

// Packet type tags 
// The first byte of every ESP-NOW payload identifies what follows.
#define PKT_TYPE_HEADER   0xA0   // SessionHeader_t
#define PKT_TYPE_RECORD   0xA1   // ProbeRecord_t
#define PKT_TYPE_EOF      0xA2   // No payload — signals end of transfer
#define PKT_TYPE_ACK      0xAC   // Sent by receiver back to probe (acknowledgement)
#define PKT_TYPE_NACK     0xAE   // Sent by receiver — requests retransmit

// =============================================================================

// Session header 
// Sent once before any records. Receiver uses record_count to track progress.
typedef struct __attribute__((packed)) {
    uint8_t  pkt_type;          // Always PKT_TYPE_HEADER (0xA0)
    uint32_t record_count;      // Total records that will follow
    uint32_t session_start_ms;  // millis() at first sensor reading this session
    uint32_t session_date;      // YYYYMMDD format (added to match espnow_transfer.cpp)
    uint8_t  firmware_version;  // Bump when wire format changes
    uint8_t  reserved[2];       // Pad
} SessionHeader_t;

// =============================================================================

// Per-record payload 
// This is ProbeRecord_t which is mentioned in the other files
typedef struct __attribute__((packed)) {
    uint8_t  pkt_type;          // Always PKT_TYPE_RECORD (0xA1)
    uint32_t entry_num;         // Monotonically increasing record index (0-based)
    uint32_t ms_since_start;    // Milliseconds since session_start_ms
    float    temperature_c;     // °C — from sensor (SS4)
    float    pressure_dbar;     // dbar — from pressure sensor (SS4)
    float    spec_channels[11]; // 11 Spectrometer channels (s6=excitation, s7=fluorescence)
    uint8_t  checksum;          // XOR of all preceding bytes in this struct
} ProbeRecord_t;

// =============================================================================

// EOF marker 
// Just the type byte — no other payload needed.
typedef struct __attribute__((packed)) {
    uint8_t pkt_type;           // Always PKT_TYPE_EOF (0xA2)
} EofMarker_t;

// =============================================================================

// ACK/NACK from receiver 
typedef struct __attribute__((packed)) {
    uint8_t  pkt_type;          // PKT_TYPE_ACK or PKT_TYPE_NACK
    uint32_t entry_num;         // Which record this is acknowledging / requesting retry
} ReceiverAck_t;

// =============================================================================

// Compile-time size checks 
static_assert(sizeof(SessionHeader_t) == 16, "SessionHeader_t size mismatch");
static_assert(sizeof(ProbeRecord_t)   == 62, "ProbeRecord_t size mismatch");

// =============================================================================

// Checksum helper 
// XOR of all bytes in buf[0..len-1].
// For ProbeRecord_t: call with len = sizeof(ProbeRecord_t) - 1 (exclude last byte).
static inline uint8_t xor_checksum(const uint8_t* buf, uint8_t len) {
    uint8_t cs = 0;
    for (uint8_t i = 0; i < len; i++) cs ^= buf[i];
    return cs;
}

// =============================================================================

//  Record builder 
// Fills and checksums a ProbeRecord_t from real-unit values.
// Call this when building records to be stored in FRAM or sent over ESP-NOW.
static inline ProbeRecord_t build_record(
    uint32_t entry_num,
    uint32_t ms_since_start,
    float    temperature_c,
    float    pressure_dbar,
    const float* spec_channels
) {
    ProbeRecord_t r;    // r is a struct of type ProbeRecord_t
    r.pkt_type         = PKT_TYPE_RECORD;
    r.entry_num        = entry_num;
    r.ms_since_start   = ms_since_start;
    r.temperature_c    = temperature_c;
    r.pressure_dbar    = pressure_dbar;
    for (int i = 0; i < 11; i++) {
        r.spec_channels[i] = spec_channels[i];
    }
    r.checksum         = xor_checksum((const uint8_t*)&r, sizeof(r) - 1);
    return r;
}

// =============================================================================

// Checksum validator 
static inline bool record_valid(const ProbeRecord_t* r) {
    return xor_checksum((const uint8_t*)r, sizeof(*r) - 1) == r->checksum;
}

// =============================================================================