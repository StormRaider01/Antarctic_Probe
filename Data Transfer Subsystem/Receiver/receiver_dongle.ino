/**
 * receiver_dongle.ino
 * ===================
 * Firmware for the RECEIVER side — the second ESP32-C6 plugged into the laptop.
 *
 * Responsibilities:
 *   1. Receive ESP-NOW packets from the probe
 *   2. Validate each ProbeRecord_t checksum
 *   3. Send ACK back to probe for each valid record
 *   4. Forward records to the laptop over USB Serial as CSV lines
 *
 * CSV format output to laptop (one line per record):
 *   entry_num,ms_since_start,temperature_c,pressure_dbar,excitation_raw,fluorescence_raw
 *
 * The Python GUI reads these lines from the Serial port.
 *
 * ── BEFORE DEPLOYING ─────────────────────────────────────────────────────
 * Run this sketch once and note the MAC address printed to Serial.
 * Paste it into RECEIVER_MAC[] in espnow_transfer.cpp on the probe side.
 *
 * ── Serial port settings ─────────────────────────────────────────────────
 * Baud: 115200. Joshua's GUI opens this port and reads lines starting with
 * "DATA:" — all other lines (status, errors) start with "[" and can be
 * filtered out or displayed in a log pane.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "data_packet.h"    // shared wire format — copy this file to dongle sketch folder

// ── MAC address of the probe (sender) — set after reading probe's MAC ─────
// Leave as broadcast (FF:FF:FF:FF:FF:FF) during initial testing.
// For production, set this to the probe's actual MAC to reject other senders.
static const uint8_t PROBE_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ── Session state ──────────────────────────────────────────────────────────
static uint32_t s_expected_count  = 0;
static uint32_t s_received_count  = 0;
static uint32_t s_session_start   = 0;
static bool     s_session_active  = false;

// ── Send ACK back to probe ─────────────────────────────────────────────────
static void send_ack(const uint8_t* probe_mac, uint32_t entry_num, bool ok) {
    ReceiverAck_t ack;
    ack.pkt_type  = ok ? PKT_TYPE_ACK : PKT_TYPE_NACK;
    ack.entry_num = entry_num;
    esp_now_send(probe_mac, (const uint8_t*)&ack, sizeof(ack));
}

// ── ESP-NOW receive callback ───────────────────────────────────────────────
static void on_receive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (len < 1) return;

    uint8_t pkt_type = data[0];

    // ── Session header ─────────────────────────────────────────────────────
    if (pkt_type == PKT_TYPE_HEADER && len >= (int)sizeof(SessionHeader_t)) {
        const SessionHeader_t* hdr = (const SessionHeader_t*)data;
        s_expected_count  = hdr->record_count;
        s_session_start   = hdr->session_start_ms;
        s_received_count  = 0;
        s_session_active  = true;
        Serial.printf("[SESSION] Started. Expecting %lu records. FW v%u.\n",
                      s_expected_count, hdr->firmware_version);
        return;
    }

    // ── Data record ────────────────────────────────────────────────────────
    if (pkt_type == PKT_TYPE_RECORD && len >= (int)sizeof(ProbeRecord_t)) {
        const ProbeRecord_t* rec = (const ProbeRecord_t*)data;

        // Validate checksum
        if (!record_valid(rec)) {
            Serial.printf("[ERROR] Checksum fail: entry %lu — sending NACK.\n", rec->entry_num);
            send_ack(info->src_addr, rec->entry_num, false);
            return;
        }

        // Send ACK
        send_ack(info->src_addr, rec->entry_num, true);
        s_received_count++;

        // Output CSV to laptop Serial
        // Format: DATA:entry,ms_since_start,temp,pressure,spec0...spec10
        Serial.printf("DATA:%lu,%lu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
                      rec->entry_num,
                      rec->ms_since_start,
                      rec->temperature_c,
                      rec->pressure_dbar,
                      rec->spec_channels[0],
                      rec->spec_channels[1],
                      rec->spec_channels[2],
                      rec->spec_channels[3],
                      rec->spec_channels[4],
                      rec->spec_channels[5],
                      rec->spec_channels[6],
                      rec->spec_channels[7],
                      rec->spec_channels[8],
                      rec->spec_channels[9],
                      rec->spec_channels[10]);
        return;
    }

    // ── EOF marker ─────────────────────────────────────────────────────────
    if (pkt_type == PKT_TYPE_EOF) {
        Serial.printf("[SESSION] EOF received. Got %lu / %lu records.\n",
                      s_received_count, s_expected_count);
        s_session_active = false;
        return;
    }
}

// ── setup ──────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n===== SS1 Receiver Dongle =====");

    // Print MAC so you can paste it into the probe firmware
    WiFi.mode(WIFI_STA);
    Serial.print("[INFO] Receiver MAC: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERROR] esp_now_init() failed. Halting.");
        while (1);
    }

    esp_now_register_recv_cb(on_receive);

    // Register probe as peer so we can send ACKs back
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, PROBE_MAC, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);   // may fail if PROBE_MAC is broadcast — that's OK for testing

    Serial.println("[INFO] Ready. Waiting for probe...");
}

void loop() {
    // ESP-NOW callbacks handle everything.
    // Nothing needed here — add a heartbeat print if useful for debugging.
    
    // Add a heartbeat print so the Python script can "find" it anytime
    Serial.println("[INFO] Ready. Waiting for probe...");
    delay(1000);
}
