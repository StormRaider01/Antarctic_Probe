#include <Arduino.h>
#include "fram_manager.h"
#include "espnow_transfer.h"

// ==============================================================================
// 1. HITL DEBUG CONFIGURATION
// ==============================================================================
#define DEBUG_MODE 1  // Set to 0 for production

#if DEBUG_MODE
    #define SLEEP_DURATION_SEC 5  // Accelerated 5s for HITL testing
    #define DEBUG_PRINT(x) Serial.print(x)
    #define DEBUG_PRINTLN(x) Serial.println(x)
    #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
    #define SLEEP_DURATION_SEC (5 * 60) // 5 minutes for production
    #define DEBUG_PRINT(x)
    #define DEBUG_PRINTLN(x)
    #define DEBUG_PRINTF(...)
#endif

#define REED_SWITCH_PIN GPIO_NUM_7  // GPIO 9 (BOOT) is NOT an RTC pin on C6. Using GPIO 7.
#define uS_TO_S_FACTOR 1000000ULL

// Simulated Record Number Tracker (survives deep sleep)
RTC_DATA_ATTR int record_counter = 1;
RTC_DATA_ATTR uint32_t session_start_time = 0; // ms

void setup() {
    Serial.begin(115200);
    delay(2000); // Increased delay to allow USB-Serial JTAG to reconnect on PC
    
    Serial.println("\n\n--- ESP32-C6 Antarctic Probe Firmware ---");
    #if DEBUG_MODE
    Serial.println("[HITL] DEBUG_MODE ACTIVE (Timer: 5s, Reed: GPIO 7)");
    #endif

    fram_init();

    // Determine wake-up cause and route to the correct state
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        // --- STATE: WAKE_AND_LOG ---
        #if DEBUG_MODE
        Serial.println("[HITL] Waking up (Timer)...");
        Serial.println("[HITL] Generating dummy reading...");
        #else
        Serial.println("Wakeup Reason: TIMER. State -> WAKE_AND_LOG");
        #endif
        
        uint32_t current_time_ms = (record_counter - 1) * SLEEP_DURATION_SEC * 1000;
        
        // Generate a simulated 15-value data string.
        String dummy_data = String(record_counter) + "," + String(current_time_ms) + ",1400.0,2200.0,100.0,105.0,90.0,110.0,95.0,100.0,120.0,80.0,90.0,110.0,105.0";
        
        #if DEBUG_MODE
        Serial.println("[HITL] Saving to F-RAM: " + dummy_data);
        #endif
        
        fram_write_record(dummy_data);
        record_counter++;
        
    } else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
        // --- STATE: OFFLOAD ---
        #if DEBUG_MODE
        Serial.println("[HITL] WAKEUP CAUSE: GPIO 7 (Magnetic Switch Simulated)!");
        Serial.println("[HITL] Offloading F-RAM contents...");
        #else
        Serial.println("Wakeup Reason: REED SWITCH (EXT). State -> OFFLOAD");
        #endif
        
        ProbeRecord_t records[100];
        int count = fram_get_records(records, 100);
        
        if (count > 0) {
            #if DEBUG_MODE
            Serial.printf("[HITL] Retrieving %d records from RTC Memory...\n", count);
            #endif
            
            ESPNowStatus_t status = ESPNOW_Init();
            if (status == ESPNOW_OK) {
                ESPNOW_StartTransfer(records, count, session_start_time, 20260511);
                ESPNOW_Deinit();
            } else {
                Serial.printf("ESP-NOW Init failed: %d\n", status);
            }
        } else {
            Serial.println("No records to offload.");
        }
        
        fram_clear();
        record_counter = 1;
        session_start_time = millis(); // Reset session
        
    } else {
        Serial.println("Wakeup Reason: OTHER (Initial Boot/Reset).");
        session_start_time = millis();
        record_counter = 1; // Explicitly reset counter on physical reset/initial boot
    }

    // --- STATE: DEEP_SLEEP ---
    #if DEBUG_MODE
    Serial.printf("[HITL] Going to deep sleep for %ds...\n", SLEEP_DURATION_SEC);
    #endif
    
    pinMode(REED_SWITCH_PIN, INPUT_PULLUP);
    
    // Configure wake-up sources
    // Note: On ESP32-C6, only LP GPIOs (0-7) can wake from deep sleep.
    esp_sleep_enable_ext1_wakeup(1ULL << REED_SWITCH_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_SEC * uS_TO_S_FACTOR);

    Serial.flush();
    delay(100); // Give UART/USB-Serial JTAG buffer time to flush before power-down
    esp_deep_sleep_start();
}

void loop() {}

