/**
 * data_packet.h
 * =============
 * Binary wire format shared between the ESP32-C6 probe (sender) and the
 * receiver dongle ESP32 (which forwards to the laptop Python GUI).
 *
 * All multi-byte fields are LITTLE-ENDIAN (native to ESP32 and x86).
 *
 * SESSION HEADER (sent once at start of transfer, 16 bytes):
 *   Lets the receiver know how many records to expect and when the session started.
 *
 * PROBE RECORD (62 bytes per measurement):
 *   Byte offset map (all multi-byte fields little-endian):
 *   [0]      uint8   pkt_type       — always PKT_TYPE_RECORD (0xA1)
 *   [1:5]    uint32  entry_num      — record index (0-based)
 *   [5:9]    uint32  ms_since_start — ms since session_start_ms
 *   [9:13]   float32 temperature_c  — °C
 *   [13:17]  float32 pressure_dbar  — dbar
 *   [17:21]  float32 spec1          — spectrometer ch1
 *   [21:25]  float32 spec2          — spectrometer ch2
 *   [25:29]  float32 spec3          — spectrometer ch3
 *   [29:33]  float32 spec4          — spectrometer ch4
 *   [33:37]  float32 spec5          — spectrometer ch5
 *   [37:41]  float32 spec6          — spectrometer ch6 (excitation)
 *   [41:45]  float32 spec7          — spectrometer ch7 (fluorescence)
 *   [45:49]  float32 spec8          — spectrometer ch8
 *   [49:53]  float32 spec9          — spectrometer ch9
 *   [53:57]  float32 spec10         — spectrometer ch10
 *   [57:61]  float32 spec11         — spectrometer ch11
 *   [61]     uint8   checksum       — XOR of bytes [0:61]
 *
 * Checksum: XOR of all 61 preceding bytes. Receiver rejects packet if mismatch.
 *
 * Chlorophyll-A is NOT computed onboard. The laptop derives it from:
 *   spec6 (excitation) and spec7 (fluorescence) post-hoc (SS2's ML processing step).
 *
 * ESP-NOW MTU = 250 bytes. One record (62 bytes) fits easily.
 */

#pragma once
#include <stdint.h>

// ── Packet type tags ───────────────────────────────────────────────────────
// The first byte of every ESP-NOW payload identifies what follows.
#define PKT_TYPE_HEADER   0xA0   // SessionHeader_t
#define PKT_TYPE_RECORD   0xA1   // ProbeRecord_t
#define PKT_TYPE_EOF      0xA2   // No payload — signals end of transfer
#define PKT_TYPE_ACK      0xAC   // Sent by receiver back to probe (acknowledgement)
#define PKT_TYPE_NACK     0xAE   // Sent by receiver — requests retransmit

// ── Session header ─────────────────────────────────────────────────────────
// Sent once before any records. Receiver uses record_count to track progress.
typedef struct __attribute__((packed)) {
    uint8_t  pkt_type;          // Always PKT_TYPE_HEADER (0xA0)
    uint32_t record_count;      // Total records that will follow
    uint32_t session_start_ms;  // millis() at first sensor reading this session
    uint32_t session_date;      // YYYYMMDD format
    uint8_t  firmware_version;  // Bump when wire format changes
    uint8_t  reserved[2];       // Pad to 16 bytes — set to 0
} SessionHeader_t;

// ── Per-record payload ─────────────────────────────────────────────────────
typedef struct __attribute__((packed)) {
    uint8_t  pkt_type;          // Always PKT_TYPE_RECORD (0xA1)
    uint32_t entry_num;         // Monotonically increasing record index (0-based)
    uint32_t ms_since_start;    // Milliseconds since session_start_ms
    float    temperature_c;     // °C — from sensor (SS4)
    float    pressure_dbar;     // dbar — from pressure sensor (SS4)
    float    spec1;             // Spectrometer channel 1
    float    spec2;             // Spectrometer channel 2
    float    spec3;             // Spectrometer channel 3
    float    spec4;             // Spectrometer channel 4
    float    spec5;             // Spectrometer channel 5
    float    spec6;             // Spectrometer channel 6 — excitation
    float    spec7;             // Spectrometer channel 7 — fluorescence
    float    spec8;             // Spectrometer channel 8
    float    spec9;             // Spectrometer channel 9
    float    spec10;            // Spectrometer channel 10
    float    spec11;            // Spectrometer channel 11
    uint8_t  checksum;          // XOR of all preceding bytes in this struct
} ProbeRecord_t;

// ── EOF marker ────────────────────────────────────────────────────────────
// Just the type byte — no other payload needed.
typedef struct __attribute__((packed)) {
    uint8_t pkt_type;           // Always PKT_TYPE_EOF (0xA2)
} EofMarker_t;

// ── ACK/NACK from receiver ────────────────────────────────────────────────
typedef struct __attribute__((packed)) {
    uint8_t  pkt_type;          // PKT_TYPE_ACK or PKT_TYPE_NACK
    uint32_t entry_num;         // Which record this is acknowledging / requesting retry
} ReceiverAck_t;

// =============================================================================

// Compile-time size checks
static_assert(sizeof(SessionHeader_t) == 16, "SessionHeader_t size mismatch");
static_assert(sizeof(ProbeRecord_t)   == 62, "ProbeRecord_t size mismatch");

// ── Checksum helper ───────────────────────────────────────────────────────
// XOR of all bytes in buf[0..len-1].
// For ProbeRecord_t: call with len = sizeof(ProbeRecord_t) - 1 (exclude last byte).
static inline uint8_t xor_checksum(const uint8_t* buf, uint8_t len) {
    uint8_t cs = 0;
    for (uint8_t i = 0; i < len; i++) cs ^= buf[i];
    return cs;
}

// ── Record builder ────────────────────────────────────────────────────────
// Fills and checksums a ProbeRecord_t from real-unit values.
// Call this when building records to be stored in FRAM or sent over ESP-NOW.
static inline ProbeRecord_t build_record(
    uint32_t entry_num,
    uint32_t ms_since_start,
    float    temperature_c,
    float    pressure_dbar,
    float    spec1,
    float    spec2,
    float    spec3,
    float    spec4,
    float    spec5,
    float    spec6,
    float    spec7,
    float    spec8,
    float    spec9,
    float    spec10,
    float    spec11
) {
    ProbeRecord_t r;
    r.pkt_type       = PKT_TYPE_RECORD;
    r.entry_num      = entry_num;
    r.ms_since_start = ms_since_start;
    r.temperature_c  = temperature_c;
    r.pressure_dbar  = pressure_dbar;
    r.spec1          = spec1;
    r.spec2          = spec2;
    r.spec3          = spec3;
    r.spec4          = spec4;
    r.spec5          = spec5;
    r.spec6          = spec6;
    r.spec7          = spec7;
    r.spec8          = spec8;
    r.spec9          = spec9;
    r.spec10         = spec10;
    r.spec11         = spec11;
    r.checksum       = xor_checksum((const uint8_t*)&r, sizeof(r) - 1);
    return r;
}

// ── Checksum validator ────────────────────────────────────────────────────
static inline bool record_valid(const ProbeRecord_t* r) {
    return xor_checksum((const uint8_t*)r, sizeof(*r) - 1) == r->checksum;
}
