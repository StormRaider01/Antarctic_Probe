#include <Arduino.h>
#include "fram_manager.h"
#include "espnow_transfer.h"
#include "data_packet.h"

// ==============================================================================
// 1. HITL DEBUG CONFIGURATION
// ==============================================================================
#define DEBUG_MODE 1  // Set to 0 for production

#if DEBUG_MODE
    #define SLEEP_DURATION_SEC 5  // Accelerated 5s for HITL testing
#else
    #define SLEEP_DURATION_SEC (5 * 60) // 5 minutes for production
#endif

// PIN DEFINITION
#define REED_SWITCH_PIN GPIO_NUM_1  // D6 on FireBeetle 2 C6
#define uS_TO_S_FACTOR 1000000ULL

// Simulated State Persistence
RTC_DATA_ATTR int record_counter = 1;
RTC_DATA_ATTR uint32_t session_start_time = 0; // ms

void handle_offload() {
    #if DEBUG_MODE
    Serial.println("[HITL] STATE -> OFFLOAD: Retrieving records from F-RAM...");
    #endif
    
    ProbeRecord_t* records = (ProbeRecord_t*)malloc(sizeof(ProbeRecord_t) * 100);
    if (records != NULL) {
        int count = fram_get_records(records, 100);
        if (count > 0) {
            ESPNowStatus_t status = ESPNOW_Init();
            if (status == ESPNOW_OK) {
                // Use a dynamic or constant date
                uint32_t current_date = 20260512; 
                ESPNOW_StartTransfer(records, count, session_start_time, current_date);
                ESPNOW_Deinit();
            } else {
                Serial.printf("ESP-NOW Init failed: %d\n", status);
            }
        } else {
            Serial.println("No records found in F-RAM memory.");
        }
        free(records);
    }
    
    fram_clear();
    record_counter = 1;
    session_start_time = millis(); 
}

void setup() {
    Serial.begin(115200);
    
    // Wait for Serial to initialize
    uint32_t start_wait = millis();
    while (!Serial && (millis() - start_wait < 4000));
    delay(500); 
    
    Serial.println("\n\n--- ESP32-C6 Antarctic Probe Firmware ---");
    
    esp_reset_reason_t reset_reason = esp_reset_reason();
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    Serial.printf("Diagnostics -> Reset Reason: %d, Wakeup Cause: %d\n", (int)reset_reason, (int)wakeup_cause);

    fram_init();
    pinMode(REED_SWITCH_PIN, INPUT_PULLUP);

    bool trigger_offload = false;

    // Check if we woke up from the button
    if (wakeup_cause == ESP_SLEEP_WAKEUP_EXT1 || wakeup_cause == ESP_SLEEP_WAKEUP_GPIO) {
        trigger_offload = true;
    } 
    // Check if it's a cycle wakeup (Timer or Spurious Cause 0)
    else if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER || (reset_reason == ESP_RST_DEEPSLEEP && wakeup_cause == ESP_SLEEP_WAKEUP_UNDEFINED)) {
        #if DEBUG_MODE
        Serial.printf("[HITL] Cycle interval delay (%ds). Press button to offload...\n", SLEEP_DURATION_SEC);
        uint32_t wait_start = millis();
        while (millis() - wait_start < (SLEEP_DURATION_SEC * 1000)) {
            if (digitalRead(REED_SWITCH_PIN) == LOW) {
                trigger_offload = true;
                break;
            }
            delay(10);
        }
        #endif

        if (!trigger_offload) {
            // --- STATE: WAKE_AND_LOG ---
            #if DEBUG_MODE
            Serial.println("[HITL] Waking up (Timer/Cycle)...");
            #endif
            
            uint32_t current_time_ms = (record_counter - 1) * SLEEP_DURATION_SEC * 1000;
            String dummy_data = String(record_counter) + "," + String(current_time_ms) + ",1400.0,2200.0,100.0,105.0,90.0,110.0,95.0,100.0,120.0,80.0,90.0,110.0,105.0";
            
            #if DEBUG_MODE
            Serial.println("[HITL] Saving to F-RAM: " + dummy_data);
            #endif
            
            fram_write_record(dummy_data);
            record_counter++;
        }
    } else {
        // INITIAL BOOT
        Serial.println("System Start: (Initial Power-On).");
        if (session_start_time == 0) session_start_time = millis();
    }

    if (trigger_offload) {
        handle_offload();
    }

    // --- STATE: DEEP_SLEEP ---
    #if DEBUG_MODE
    Serial.printf("[HITL] Entering DEEP SLEEP for %ds...\n", SLEEP_DURATION_SEC);
    #endif
    
    esp_sleep_enable_ext1_wakeup(1ULL << REED_SWITCH_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_SEC * uS_TO_S_FACTOR);

    Serial.flush();
    delay(1000); 
    esp_deep_sleep_start();
}

void loop() {}
