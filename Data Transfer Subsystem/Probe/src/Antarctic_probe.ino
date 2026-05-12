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

// Simulated State Persistence (survives deep sleep)
RTC_DATA_ATTR int record_counter = 1;
RTC_DATA_ATTR uint32_t session_start_time = 0; 

void handle_offload() {
    Serial.println("\n[HITL] WAKEUP CAUSE: GPIO 1 (Magnetic Switch Simulated)!");
    Serial.println("[HITL] STATE -> OFFLOAD: Retrieving records from F-RAM...");
    
    ProbeRecord_t* records = (ProbeRecord_t*)malloc(sizeof(ProbeRecord_t) * 100);
    if (records != NULL) {
        int count = fram_get_records(records, 100);
        if (count > 0) {
            ESPNowStatus_t status = ESPNOW_Init();
            if (status == ESPNOW_OK) {
                uint32_t current_date = 20260512; 
                ESPNOW_StartTransfer(records, count, session_start_time, current_date);
                ESPNOW_Deinit();
            } else {
                Serial.printf("[ESPNOW] Init failed: %d\n", status);
            }
        } else {
            Serial.println("[OFFLOAD] No records found in F-RAM memory.");
        }
        free(records);
    }
    
    fram_clear();
    record_counter = 1;
    session_start_time = millis(); 
}

void setup() {
    // 1. Determine reset reason immediately
    esp_reset_reason_t reset_reason = esp_reset_reason();
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    pinMode(REED_SWITCH_PIN, INPUT_PULLUP);

    bool is_deepsleep_reset = (reset_reason == ESP_RST_DEEPSLEEP);
    bool is_button_wakeup = (wakeup_cause == ESP_SLEEP_WAKEUP_EXT1 || wakeup_cause == ESP_SLEEP_WAKEUP_GPIO);
    
    // 2. Initialize Serial
    Serial.begin(115200);
    
    // 3. Handle Initial Boot / Manual Reset (Clean Restart)
    if (!is_deepsleep_reset) {
        // We MUST wait for Serial to connect so the user sees the start messages
        uint32_t start_wait = millis();
        while (!Serial && (millis() - start_wait < 3000));
        delay(500); 

        Serial.println("\n\n=================================================");
        Serial.println("--- ESP32-C6 Antarctic Probe Firmware ---");
        Serial.println("SYSTEM START: (Initial Power-On / Manual Reset)");
        Serial.printf("Diagnostics -> Reset Reason: %d\n", (int)reset_reason);
        Serial.println("=================================================");
        
        fram_init();
        fram_clear();
        record_counter = 1;
        session_start_time = millis();
        
    } else {
        // It's a Deep Sleep reset. Determine if it's a silent cycle or a button trigger.
        bool trigger_offload = is_button_wakeup;
        bool is_cycle = !is_button_wakeup;

        if (is_cycle) {
            // SILENT POLL: Check for button during the 5s interval
            uint32_t wait_start = millis();
            while (millis() - wait_start < (SLEEP_DURATION_SEC * 1000)) {
                if (digitalRead(REED_SWITCH_PIN) == LOW) {
                    trigger_offload = true;
                    break;
                }
                delay(10);
            }
        }

        // Wait briefly for Serial to reconnect after sleep wakeup
        uint32_t start_wait = millis();
        while (!Serial && (millis() - start_wait < 500));
        
        Serial.println("\n\n--- ESP32-C6 Antarctic Probe Firmware ---");
        Serial.printf("Diagnostics -> Reset Reason: %d, Wakeup Cause: %d\n", (int)reset_reason, (int)wakeup_cause);
        fram_init();

        if (trigger_offload) {
            handle_offload();
        } else {
            // --- STATE: WAKE_AND_LOG ---
            Serial.println("[HITL] Waking up (Timer/Cycle)...");
            
            uint32_t current_time_ms = (record_counter - 1) * SLEEP_DURATION_SEC * 1000;
            String dummy_data = String(record_counter) + "," + String(current_time_ms) + ",1400.0,2200.0,100.0,105.0,90.0,110.0,95.0,100.0,120.0,80.0,90.0,110.0,105.0";
            Serial.println("[HITL] Saving to F-RAM: " + dummy_data);
            
            fram_write_record(dummy_data);
            record_counter++;
        }
    }

    // --- STATE: DEEP_SLEEP ---
    Serial.printf("[HITL] Entering DEEP SLEEP for %ds...\n", SLEEP_DURATION_SEC);
    
    esp_sleep_enable_ext1_wakeup(1ULL << REED_SWITCH_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_SEC * uS_TO_S_FACTOR);

    Serial.flush();
    delay(1000); 
    esp_deep_sleep_start();
}

void loop() {}
