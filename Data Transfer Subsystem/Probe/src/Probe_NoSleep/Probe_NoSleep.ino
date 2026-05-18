#include <Arduino.h>
#include <WiFi.h>
#include "fram_manager.h"
#include "espnow_transfer.h"
#include "data_packet.h"

// =============================================================================
// Test sketch: probe runs continuously with no deep sleep.
// ESP-NOW is initialised immediately on boot and stays on.
// 20 dummy records are pre-loaded at startup.
// The dongle can connect and retrieve at any time via ESP-NOW commands.
// =============================================================================

#define PRELOAD_RECORDS 20

static int      record_counter     = 1;
static uint32_t session_start_time = 0;
static uint32_t session_date       = 20260518; // YYYYMMDD

int get_battery_level() { return 70; }

void log_sensor_data() {
    uint32_t ms = (uint32_t)(record_counter - 1) * 5000;
    String data = String(record_counter) + "," + String(ms) +
                  ",1400.0,2200.0,100.0,105.0,90.0,110.0,95.0,100.0,120.0,80.0,90.0,110.0,105.0";
    Serial.println("[PROBE] Logging: " + data);
    fram_write_record(data);
    record_counter++;
}

void do_offload() {
    ProbeRecord_t* records = (ProbeRecord_t*)malloc(sizeof(ProbeRecord_t) * 100);
    if (!records) {
        Serial.println("[PROBE] malloc failed.");
        return;
    }
    int count = fram_get_records(records, 100);
    if (count > 0) {
        ESPNOW_StartTransfer(records, count, session_start_time, session_date);
    } else {
        Serial.println("[PROBE] No records to send.");
    }
    free(records);

    fram_clear();
    record_counter     = 1;
    session_start_time = millis();

    Serial.printf("[PROBE] Re-loading %d records for next retrieval...\n", PRELOAD_RECORDS);
    for (int i = 0; i < PRELOAD_RECORDS; i++) {
        log_sensor_data();
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n--- Probe NoSleep Test Mode ---");

    fram_init();
    fram_clear();
    record_counter     = 1;
    session_start_time = millis();

    if (ESPNOW_Init() != ESPNOW_OK) {
        Serial.println("[ERROR] ESP-NOW init failed. Halting.");
        while (true) delay(1000);
    }

    Serial.print("[PROBE] MAC Address: ");
    Serial.println(WiFi.macAddress());

    Serial.printf("[PROBE] Pre-loading %d records...\n", PRELOAD_RECORDS);
    for (int i = 0; i < PRELOAD_RECORDS; i++) {
        log_sensor_data();
    }

    Serial.println("[PROBE] Ready. Listening for ESP-NOW commands.");
}

void loop() {
    char cmd_buf[64];
    if (ESPNOW_GetCommand(cmd_buf, sizeof(cmd_buf))) {
        String cmd = String(cmd_buf);
        Serial.printf("[CMD] Received: %s\n", cmd_buf);

        if (cmd == "[CMD]:CONNECT") {
            char resp[32];
            snprintf(resp, sizeof(resp), "[ACK]:CONNECT,BATT:%d", get_battery_level());
            ESPNOW_SendString(resp);

        } else if (cmd == "[CMD]:RETRIEVE") {
            ESPNOW_SendString("[ACK]:RETRIEVE");
            do_offload();

        } else if (cmd == "[CMD]:DISCONNECT") {
            ESPNOW_SendString("[ACK]:DISCONNECT");
        }
    }
}
