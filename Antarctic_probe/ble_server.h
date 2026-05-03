#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "data_packet.h"

/**
 
 * Public API for the BLE GATT server running on the ESP32-C6.
 *
 * Typical call sequence from the .ino:
 *   1. BLEServer_Init()          — once at boot, sets up NimBLE stack
 *   2. BLEServer_StartAdvertising() — begin advertising so laptop can find probe
 *   3. [laptop connects, reads METADATA, sends CONTROL command b'\x01']
 *   4. BLEServer_StartTransfer(packets, count) — stream records to laptop
 *   5. BLEServer_StopAdvertising() — when done, before going back to sleep
 */


// Status codes returned by BLE functions
// detect errors without relying on Serial output in production.
typedef enum {
    BLE_OK              = 0,   // Success
    BLE_ERR_NOT_INIT    = 1,   // BLEServer_Init() was never called
    BLE_ERR_NOT_CONN    = 2,   // No client connected yet
    BLE_ERR_BUSY        = 3,   // Transfer already in progress
    BLE_ERR_NIMBLE      = 4,   // NimBLE stack returned an error
} BLEStatus_t;

// Metadata payload sent when laptop reads METADATA characteristic
// Fill this struct before calling BLEServer_Init(), or update it before
// calling BLEServer_StartAdvertising() each session.
typedef struct {
    uint32_t record_count;     // How many sensor records are stored and will be sent
    uint32_t session_start_ms; // Timestamp of first record in this session (ms)
    uint8_t  firmware_version; // Simple version byte — bump when wire format changes
    uint8_t  reserved[3];      // Pad to 12 bytes for alignment (set to 0)
} ProbeMetadata_t;

// ── Global metadata instance — fill before calling BLEServer_Init() ───────
// Defined in ble_server.cpp; declared here so the .ino can write to it.
extern ProbeMetadata_t g_probe_metadata;

// ── Function prototypes ───────────────────────────────────────────────────

/**
 * BLEServer_Init()
 * ----------------
 * Initialises the NimBLE stack and registers the GATT service + four
 * characteristics (METADATA, CONTROL, DATA, STATUS).
 * Call once in setup() or equivalent.
 *
 * Returns BLE_OK on success.
 */
BLEStatus_t BLEServer_Init(void);

/**
 * BLEServer_StartAdvertising()
 * ----------------------------
 * Begins BLE advertising so the laptop can discover the probe.
 * Advertising continues until a client connects, or until
 * BLEServer_StopAdvertising() is called.
 *
 * Returns BLE_OK if advertising started successfully.
 */
BLEStatus_t BLEServer_StartAdvertising(void);

/**
 * BLEServer_StopAdvertising()
 * ---------------------------
 * Stops advertising. Call before entering deep sleep to avoid
 * wasting power keeping the radio active.
 */
void BLEServer_StopAdvertising(void);

/**
 * BLEServer_IsConnected()
 * -----------------------
 * Returns true if a laptop client is currently connected.
 * Use this in the .ino to decide when it's safe to start a transfer.
 */
bool BLEServer_IsConnected(void);

/**
 * BLEServer_StartTransfer()
 * -------------------------
 * Streams `count` pre-built SensorPacket_t structs to the connected client
 * via DATA characteristic notifications. Sends a zero-length notification
 * at the end as an end-of-transfer sentinel (matches Python stream_data()).
 *
 * Blocks until all packets are sent (or an error occurs).
 *
 * `packets` : pointer to array of SensorPacket_t (built by build_packet())
 * `count`   : number of packets to send
 *
 * Returns BLE_OK if transfer completed successfully.
 *
 * NOTE: The packets array is typically read from MRAM by Kiyuran's subsystem
 * and passed here. Agree with Kiyuran on who calls build_packet() — it could
 * be done during logging (SS2 side) or here just before transfer (SS1 side).
 */
BLEStatus_t BLEServer_StartTransfer(const SensorPacket_t* packets, uint32_t count);

/**
 * BLEServer_UpdateStatus()
 * ------------------------
 * Updates the STATUS characteristic value. The laptop can poll this to
 * monitor transfer progress (e.g. how many packets have been sent).
 * Called internally by BLEServer_StartTransfer() but can also be called
 * by the .ino to push custom status bytes.
 *
 * `status_bytes` : up to 4 bytes of status data (format is flexible)
 * `len`          : number of valid bytes in status_bytes (max 4)
 */
void BLEServer_UpdateStatus(const uint8_t* status_bytes, uint8_t len);

// Add this with the other function prototypes in ble_server.h
bool BLEServer_TransferRequested(void);