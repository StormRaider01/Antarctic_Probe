/**
 * Antarctic_probe.ino
 * ===================
 * Primary probe firmware (Subsystem 2 + Subsystem 1 Integration).
 *
 * Implements a 3-state machine:
 *   1. DEEP_SLEEP: Lowest power state.
 *   2. WAKE_AND_LOG: Wakes via timer, logs data to F-RAM, sleeps.
 *   3. OFFLOAD: Wakes via external GPIO (reed switch), transmits F-RAM via ESP-NOW, sleeps.
 */

#include <Arduino.h>
#include "fram_manager.h"
#include "espnow_transfer.h"

// Define Hardware Pins and Timers
#define REED_SWITCH_PIN GPIO_NUM_9
#define SLEEP_DURATION_SEC (5 * 60)
#define uS_TO_S_FACTOR 1000000ULL

// Simulated Record Number Tracker (survives deep sleep)
RTC_DATA_ATTR int record_counter = 1;
RTC_DATA_ATTR uint32_t session_start_time = 0; // ms

void setup() {
    Serial.begin(115200);
    delay(100); 
    
    Serial.println("\n\n--- ESP32-C6 Antarctic Probe Firmware ---");

    fram_init();

    // Determine wake-up cause and route to the correct state
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        // --- STATE: WAKE_AND_LOG ---
        Serial.println("Wakeup Reason: TIMER. State -> WAKE_AND_LOG");
        
        uint32_t current_time_ms = (record_counter - 1) * SLEEP_DURATION_SEC * 1000;
        
        // Generate a simulated 15-value data string.
        // RecordNum, ms_since_start, Temp, Pressure, 11x Spectrometer (s6=excitation, s7=fluorescence)
        String dummy_data = String(record_counter) + "," + String(current_time_ms) + ",1400.0,2200.0,100.0,105.0,90.0,110.0,95.0,100.0,120.0,80.0,90.0,110.0,105.0";
        
        fram_write_record(dummy_data);
        record_counter++;
        
    } else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 || wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
        // --- STATE: OFFLOAD ---
        Serial.println("Wakeup Reason: REED SWITCH (EXT). State -> OFFLOAD");
        
        ProbeRecord_t records[100];
        int count = fram_get_records(records, 100);
        
        if (count > 0) {
            Serial.printf("Offloading %d records via ESP-NOW...\n", count);
            
            ESPNowStatus_t status = ESPNOW_Init();
            if (status == ESPNOW_OK) {
                // Pass records to ESP-NOW broadcast function
                ESPNOW_StartTransfer(records, count, session_start_time, 0);
                ESPNOW_Deinit();
            } else {
                Serial.printf("ESP-NOW Init failed: %d\n", status);
            }
        } else {
            Serial.println("No records to offload.");
        }
        
        // Clear F-RAM and reset counter
        fram_clear();
        record_counter = 1;
        session_start_time = millis(); // Reset session
        
    } else {
        // INITIAL BOOT
        Serial.println("Wakeup Reason: OTHER (Initial Boot/Reset).");
        session_start_time = millis();
    }

    // --- STATE: DEEP_SLEEP ---
    Serial.println("State -> DEEP_SLEEP. Entering low-power mode...");
    
    pinMode(REED_SWITCH_PIN, INPUT_PULLUP);
    
    esp_sleep_enable_ext0_wakeup(REED_SWITCH_PIN, 0); // Wake on logic LOW
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_SEC * uS_TO_S_FACTOR);

    Serial.flush();
    esp_deep_sleep_start();
}

void loop() {}
