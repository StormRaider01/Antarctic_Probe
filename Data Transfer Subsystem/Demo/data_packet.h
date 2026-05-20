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
 * PROBE RECORD (58 bytes per measurement):
 *   Byte offset map (all multi-byte fields little-endian):
 *   [0]      uint8   pkt_type       — always PKT_TYPE_RECORD (0xA1)
 *   [1:5]    uint32  reading        — record ID (spans 11 rows)
 *   [5:9]    uint32  time_ms        — ms since session_start_ms
 *   [9:13]   float32 temp_c         — °C
 *   [13:17]  float32 depth_m        — meters
 *   [17:21]  float32 F1_415         — Spectrometer F1
 *   [21:25]  float32 F2_445         — Spectrometer F2 (excitation)
 *   [25:29]  float32 F3_480         — Spectrometer F3
 *   [29:33]  float32 F4_515         — Spectrometer F4
 *   [33:37]  float32 F5_555         — Spectrometer F5
 *   [37:41]  float32 F6_590         — Spectrometer F6
 *   [41:45]  float32 F7_630         — Spectrometer F7
 *   [45:49]  float32 F8_680         — Spectrometer F8 (chlorophyll-a)
 *   [49:53]  float32 clear          — Spectrometer Clear
 *   [53:57]  float32 NIR            — Spectrometer NIR
 *   [57]     uint8   checksum       — XOR of bytes [0:57]
 *
 * Checksum: XOR of all 57 preceding bytes. Receiver rejects packet if mismatch.
 *
 * Chlorophyll-A and XAI classification is NOT computed onboard. The laptop derives it from:
 *   XAI ratios against F2_445 (SS2's ML processing step).
 *
 * ESP-NOW MTU = 250 bytes. One record (58 bytes) fits easily.
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
    uint32_t reading;           // Record index/ID (spans 11 rows)
    uint32_t time_ms;           // Milliseconds since session_start_ms
    float    temp_c;            // °C — from sensor
    float    depth_m;           // meters — from pressure sensor
    float    F1_415;            // Spectrometer channel 1
    float    F2_445;            // Spectrometer channel 2 (excitation)
    float    F3_480;            // Spectrometer channel 3
    float    F4_515;            // Spectrometer channel 4
    float    F5_555;            // Spectrometer channel 5
    float    F6_590;            // Spectrometer channel 6
    float    F7_630;            // Spectrometer channel 7
    float    F8_680;            // Spectrometer channel 8 (Chlorophyll-a)
    float    clear;             // Spectrometer Clear channel
    float    NIR;               // Spectrometer NIR channel
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
static_assert(sizeof(ProbeRecord_t)   == 58, "ProbeRecord_t size mismatch");

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
    uint32_t reading,
    uint32_t time_ms,
    float    temp_c,
    float    depth_m,
    float    F1_415,
    float    F2_445,
    float    F3_480,
    float    F4_515,
    float    F5_555,
    float    F6_590,
    float    F7_630,
    float    F8_680,
    float    clear,
    float    NIR
) {
    ProbeRecord_t r;
    r.pkt_type       = PKT_TYPE_RECORD;
    r.reading        = reading;
    r.time_ms        = time_ms;
    r.temp_c         = temp_c;
    r.depth_m        = depth_m;
    r.F1_415         = F1_415;
    r.F2_445         = F2_445;
    r.F3_480         = F3_480;
    r.F4_515         = F4_515;
    r.F5_555         = F5_555;
    r.F6_590         = F6_590;
    r.F7_630         = F7_630;
    r.F8_680         = F8_680;
    r.clear          = clear;
    r.NIR            = NIR;
    r.checksum       = xor_checksum((const uint8_t*)&r, sizeof(r) - 1);
    return r;
}

// ── Checksum validator ────────────────────────────────────────────────────
static inline bool record_valid(const ProbeRecord_t* r) {
    return xor_checksum((const uint8_t*)r, sizeof(*r) - 1) == r->checksum;
}
