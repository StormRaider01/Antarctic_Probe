/**
 * espnow_transfer.cpp
 * ===================
 * ESP-NOW data transfer implementation for the probe (sender) side.
 *
 * Architecture:
 *   - Probe sends ProbeRecord_t structs one at a time.
 *   - Receiver dongle sends back a ReceiverAck_t for each record.
 *   - If no ACK arrives within ESPNOW_ACK_TIMEOUT_MS, probe retries
 *     up to ESPNOW_MAX_RETRIES times, then skips (gap visible via entry_num).
 *   - A SessionHeader_t is sent first, an EofMarker_t last.
 *
 * ── BEFORE DEPLOYING ─────────────────────────────────────────────────────
 * Set RECEIVER_MAC to the MAC address of the receiver dongle ESP32.
 * Read it by running this on the dongle once:
 *   #include <WiFi.h>
 *   void setup() { Serial.begin(115200); Serial.println(WiFi.macAddress()); }
 *   void loop() {}
 */

#include "espnow_transfer.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// ===========================================================================
// Receiver MAC address
// ** CHANGE THIS to the actual MAC of the receiver dongle before deploying **
// Updated to Kiyuran's board as per TestProbe.ino
// static const uint8_t RECEIVER_MAC[6] = {0x9C, 0x13, 0x9E, 0xCC, 0x35, 0x50};
static const uint8_t RECEIVER_MAC[6] = {0x88, 0x57, 0x21, 0x2E, 0xA8, 0x58}; // new WEMOS LOLIN board with pins soldered

// ===========================================================================

// Tuning constants 
#define ESPNOW_ACK_TIMEOUT_MS   500    // ms to wait for ACK per record
#define ESPNOW_MAX_RETRIES      3      // attempts per record before skipping
#define ESPNOW_INTER_SEND_MS    10     // ms gap between sends (avoids ESP-NOW overrun)

// =============================================================================

// Module-private state 
static volatile bool   s_send_success  = false;  // set by send callback
static volatile bool   s_send_done     = false;  // send callback has fired
static volatile bool   s_ack_received  = false;  // set by receive callback
static volatile uint32_t s_acked_entry = 0;       // which entry was ACKed

static char s_last_cmd[64] = {0};
static volatile bool s_cmd_available = false;
static bool s_is_init = false;

// =============================================================================

// ESP-NOW callbacks

/**
 * Called by ESP-NOW stack after esp_now_send() completes (success or fail).
 * status == ESP_NOW_SEND_SUCCESS means the packet was delivered at the MAC layer.
 * It does NOT mean the receiver's application has processed it.
 * Note: Signature changed in ESP32 Arduino Core 3.0.0
 */
static void on_send(const wifi_tx_info_t* info, esp_now_send_status_t status) {
    s_send_success = (status == ESP_NOW_SEND_SUCCESS);
    s_send_done    = true;
}

/**
 * Called when the receiver dongle sends back an ACK or NACK.
 * We only care about ReceiverAck_t payloads.
 */
static void on_receive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {

    // 1. Check if it's a structural ACK
    if (len == sizeof(ReceiverAck_t)) {
        const ReceiverAck_t* ack = (const ReceiverAck_t*)data;
        if (ack->pkt_type == PKT_TYPE_ACK) {
            s_acked_entry = ack->entry_num;
            s_ack_received = true;
            return;
        }
    }

    // 2. Otherwise treat it as a String command (like [CMD]:CONNECT)
    int copy_len = (len < 63) ? len : 63;
    memcpy(s_last_cmd, data, copy_len);
    s_last_cmd[copy_len] = '\0';
    s_cmd_available = true;
}

// =============================================================================

// Helpers

/**
 * Block until the send callback fires or timeout_ms elapses.
 * Returns true if ESP-NOW MAC-layer delivery was confirmed.
 */
static bool wait_send_done(uint32_t timeout_ms) {

    uint32_t deadline = millis() + timeout_ms;      // adds timesout to amount of milliseconds since startup
    while (!s_send_done && millis() < deadline) {
        delay(1);
    }
    return s_send_done && s_send_success;
}

/**
 * Block until an ACK arrives for `expected_entry` or timeout_ms elapses.
 */
static bool wait_ack(uint32_t expected_entry, uint32_t timeout_ms) {
    
    uint32_t deadline = millis() + timeout_ms;
    while (millis() < deadline) {

        if (s_ack_received && s_acked_entry == expected_entry) {
            // reset for next record
            s_ack_received = false;
            return true;
        }
        delay(1);
    }
    return false;
}

/**
 * Send a single packet and wait for MAC-layer confirmation.
 * Returns true if sent successfully at the MAC layer.
 */
static bool send_raw(const void* payload, uint8_t len) {

    // first reset send state
    s_send_done    = false;
    s_send_success = false;
    esp_err_t err  = esp_now_send(RECEIVER_MAC, (const uint8_t*)payload, len);

    if (err != ESP_OK) {
        Serial.printf("[ESPNOW] esp_now_send error: %d\n", err);
        return false;
    }
    return wait_send_done(ESPNOW_ACK_TIMEOUT_MS);       // wait to ensure delivery
}

// =============================================================================

// Public API

ESPNowStatus_t ESPNOW_Init(void) {

    // ESP-NOW requires WiFi to be running in station mode (radio on, not associated)
    WiFi.mode(WIFI_STA);
    delay(100);
    Serial.print("[INFO] Dongle MAC: ");
    Serial.println(WiFi.macAddress());
    WiFi.disconnect();   // ensure not connected to any AP

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] esp_now_init() failed.");
        return ESPNOW_ERR_INIT;
    }

    esp_now_register_send_cb(on_send);
    esp_now_register_recv_cb(on_receive);

    // Register receiver dongle as a peer (broadcast channel, no encryption)
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, RECEIVER_MAC, 6);
    peer.channel = 0;       // 0 = use current WiFi channel
    peer.encrypt = false;

    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("[ESPNOW] Failed to add receiver peer.");
        return ESPNOW_ERR_PEER;
    }

    s_is_init = true;
    Serial.println("[ESPNOW] Initialised. Receiver peer registered.");
    return ESPNOW_OK;
}

bool ESPNOW_IsInitialised(void) {
    return s_is_init;
}

// GUI will send marker to indicate end of transfer
int ESPNOW_Deinit(void) {
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
    s_is_init = false;
    Serial.println("[ESPNOW] Deinitialised. WiFi radio off.");
    return ESPNOW_OK;       // for overhead to know radio off before entering deep sleep
}

ESPNowStatus_t ESPNOW_SendString(const char* msg) {
    if (!send_raw(msg, strlen(msg))) {
        return ESPNOW_ERR_SEND;
    }
    return ESPNOW_OK;
}

bool ESPNOW_GetCommand(char* buf, int max_len) {
    if (!s_cmd_available) return false;
    
    strncpy(buf, s_last_cmd, max_len);
    buf[max_len - 1] = '\0';
    s_cmd_available = false; // consume it
    return true;
}

void ESPNOW_ClearCommand(void) {
    s_cmd_available = false;
    memset(s_last_cmd, 0, sizeof(s_last_cmd));
}

ESPNowStatus_t ESPNOW_StartTransfer(const ProbeRecord_t* records, uint32_t count, 
                                    uint32_t session_start_ms, uint32_t session_date) {     

    if (records == nullptr || count == 0) return ESPNOW_ERR_PARAM;

    // Send session header (main points are record count, date and start timestamp)
    SessionHeader_t header = {};
    header.pkt_type          = PKT_TYPE_HEADER;
    header.record_count      = count;
    header.session_date      = session_date;
    header.session_start_ms  = session_start_ms;
    header.firmware_version  = 1;

    Serial.printf("[ESPNOW] Sending header: %lu records, session_date=%lu, session_start=%lu ms\n",
                  count, session_date, session_start_ms);

    if (!send_raw(&header, sizeof(header))) {
        Serial.println("[ESPNOW] Header send failed.");
        return ESPNOW_ERR_SEND;
    }
    delay(ESPNOW_INTER_SEND_MS);

    // Send records one by one
    uint32_t sent_ok  = 0;
    uint32_t skipped  = 0;

    for (uint32_t i = 0; i < count; i++) {
        bool delivered = false;

        // repeat until ACK received or max retries hit
        for (uint8_t attempt = 0; attempt < ESPNOW_MAX_RETRIES; attempt++) {
            s_ack_received = false;

            if (!send_raw(&records[i], sizeof(ProbeRecord_t))) {
                Serial.printf("[ESPNOW] MAC send failed: entry %lu, attempt %u\n", i, attempt);
                delay(ESPNOW_INTER_SEND_MS);
                continue;
            }

            // Wait for application-level ACK from receiver
            if (wait_ack(records[i].entry_num, ESPNOW_ACK_TIMEOUT_MS)) {
                delivered = true;
                break;
            }

            Serial.printf("[ESPNOW] No ACK: entry %lu, attempt %u\n", i, attempt);
            delay(ESPNOW_INTER_SEND_MS);
        }

        if (delivered) {
            sent_ok++;
        } else {
            Serial.printf("[ESPNOW] Skipping entry %lu after %d retries.\n", i, ESPNOW_MAX_RETRIES);
            skipped++;
        }

        delay(ESPNOW_INTER_SEND_MS);
    }

    // Step 3: Send EOF marker
    EofMarker_t eof = { .pkt_type = PKT_TYPE_EOF };
    send_raw(&eof, sizeof(eof));   // best-effort — no ACK needed for EOF

    // For testing to see how much go through
    Serial.printf("[ESPNOW] Transfer complete. Sent: %lu, Skipped: %lu / %lu total.\n",
                  sent_ok, skipped, count);

    return ESPNOW_OK;
}

// =============================================================================