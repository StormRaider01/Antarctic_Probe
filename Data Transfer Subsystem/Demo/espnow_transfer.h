/**
 * espnow_transfer.h
 * =================
 * Public API for the ESP-NOW data transfer subsystem (SS1).
 * Kiyuran's state machine (#include "espnow_transfer.h") calls these
 * functions after detecting the reed switch trigger.
 *
 * Typical call sequence:
 *   1. ESPNOW_Init()                          — once in setup()
 *   2. [probe dives, SS2 logs records to FRAM]
 *   3. [reed switch detected by SS2 state machine]
 *   4. ESPNOW_Init()
 *   5. ESPNOW_GetCommand()                    — check for commands from dongle
 *   6. ESPNOW_SendString()                     — send ACKs/responses
 *   7. ESPNOW_StartTransfer(records, count)   — blocks until data offload done
 *   8. ESPNOW_Deinit()
 *
 * The probe side is the ESP-NOW *sender*.
 * The receiver dongle (second ESP32, plugged into laptop) is the *receiver*.
 * The receiver's MAC address must be set in espnow_transfer.cpp before deploy.
 */

#pragma once
#include <stdint.h>
#include "data_packet.h"


// =============================================================================

// Return codes
typedef enum {
    ESPNOW_OK            = 0,
    ESPNOW_ERR_INIT      = 1,   // WiFi/ESP-NOW init failed
    ESPNOW_ERR_PEER      = 2,   // Failed to add receiver as peer
    ESPNOW_ERR_SEND      = 3,   // esp_now_send() returned error
    ESPNOW_ERR_TIMEOUT   = 4,   // No ACK from receiver within timeout
    ESPNOW_ERR_PARAM     = 5,   // Null pointer or zero count passed in
} ESPNowStatus_t;

// =============================================================================
// Init
/**
 * ESPNOW_Init()
 * Initialises WiFi in station mode and registers the ESP-NOW send/receive
 * callbacks. Also registers the receiver dongle as a peer.
 * Call once in setup() or at the start of a transfer session.
 * Safe to call again after deep sleep (re-registers peer).
 */
ESPNowStatus_t ESPNOW_Init(void);

// =============================================================================
// Deinit
/**
 * ESPNOW_Deinit()
 * Tears down ESP-NOW and powers down the WiFi radio.
 * Call before entering deep sleep to save power.
 */
int ESPNOW_Deinit(void);
bool ESPNOW_IsInitialised(void);

// =============================================================================
// Command Handling
/**
 * ESPNOW_SendString()
 * Sends a raw string (e.g. command ACK) to the dongle.
 */
ESPNowStatus_t ESPNOW_SendString(const char* msg);

/**
 * ESPNOW_GetCommand()
 * Copies the last received command string into `buf`.
 * Returns true if a command was available.
 */
bool ESPNOW_GetCommand(char* buf, int max_len);

/**
 * ESPNOW_ClearCommand()
 * Resets the internal command buffer.
 */
void ESPNOW_ClearCommand(void);

// =============================================================================
// Transfer 
/**
 * ESPNOW_StartTransfer()
 * ----------------------
 * Streams `count` records to the receiver dongle, then sends an EOF marker.
 * Blocks until all records are acknowledged or a fatal error occurs.
 *
 * `records`       : pointer to array of ProbeRecord_t (from FRAM or RAM)
 * `count`         : number of records to send
 * `session_start` : millis() at the first sensor reading — included in header
 * `session_date`  : YYYYMMDD format session identifier
 *
 * Returns ESPNOW_OK if all records were delivered and acknowledged.
 *
 * Retransmit policy: each record is retried up to ESPNOW_MAX_RETRIES times
 * if no ACK is received within ESPNOW_ACK_TIMEOUT_MS. After that the record
 * is skipped (receiver detects gap via entry_num jump) and transfer continues.
 */
ESPNowStatus_t ESPNOW_StartTransfer(
    const ProbeRecord_t* records,
    uint32_t             count,
    uint32_t             session_start_ms,
    uint32_t             session_date
);

// =============================================================================