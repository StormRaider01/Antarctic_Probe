#include <Arduino.h>
#include "fram_manager.h"
#include "espnow_transfer.h"
#include "data_packet.h"

// ==============================================================================
// 1. HITL DEBUG CONFIGURATION & MISSION PARAMETERS
// ==============================================================================
#define DEBUG_MODE 1  // Set to 0 for production

#if DEBUG_MODE
    #define SINKING_DELAY_SEC 15  // HITL: 15 seconds sinking delay
    #define LOGGING_INTERVAL_SEC 5 // HITL: 5 seconds logging interval
#else
    #define SINKING_DELAY_SEC 600 // Production: 10 minutes sinking delay
    #define LOGGING_INTERVAL_SEC 300 // Production: 5 minutes logging interval
#endif

// MISSION STATES
#define STATE_STANDBY 0
#define STATE_ARMED   1
#define STATE_LOGGING 2

// PIN DEFINITION
#define REED_SWITCH_PIN GPIO_NUM_1  // D6 on FireBeetle 2 C6
#define uS_TO_S_FACTOR 1000000ULL

// Simulated State Persistence (survives deep sleep)
RTC_DATA_ATTR int mission_state = STATE_STANDBY;
RTC_DATA_ATTR int record_counter = 1;
RTC_DATA_ATTR uint32_t session_start_time = 0; 

/**
 * Executes the data offload sequence using ESP-NOW.
 * Resets the mission state to STANDBY upon completion.
 */
void handle_offload() {
    Serial.println("\n[HITL] WAKEUP CAUSE: GPIO 1 (Magnetic Switch Simulated)!");
    Serial.println("[HITL] STATE 3 (OFFLOAD): Retrieving records from F-RAM...");
    
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
    session_start_time = 0;
    mission_state = STATE_STANDBY;
    Serial.println("[HITL] Offload complete. Returning to STATE 0 (STANDBY).");
}

/**
 * Generates a dummy sensor reading and saves it to the simulated F-RAM.
 */
void log_sensor_data() {
    #if DEBUG_MODE
    Serial.println("[HITL] Waking up (Timer/Cycle)...");
    #endif
    
    uint32_t current_time_ms = (record_counter - 1) * LOGGING_INTERVAL_SEC * 1000;
    String dummy_data = String(record_counter) + "," + String(current_time_ms) + ",1400.0,2200.0,100.0,105.0,90.0,110.0,95.0,100.0,120.0,80.0,90.0,110.0,105.0";
    
    #if DEBUG_MODE
    Serial.println("[HITL] Saving to F-RAM: " + dummy_data);
    #endif
    
    fram_write_record(dummy_data);
    record_counter++;
}

void setup() {
    // 1. Immediate hardware state evaluation
    esp_reset_reason_t reset_reason = esp_reset_reason();
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    pinMode(REED_SWITCH_PIN, INPUT_PULLUP);

    bool is_deepsleep_reset = (reset_reason == ESP_RST_DEEPSLEEP);
    bool is_button_wakeup = (wakeup_cause == ESP_SLEEP_WAKEUP_EXT1 || wakeup_cause == ESP_SLEEP_WAKEUP_GPIO);
    
    // 2. Initialise Serial
    Serial.begin(115200);
    
    // 3. Handle Hard Reset (RST button / Power-on)
    if (!is_deepsleep_reset) {
        uint32_t start_wait = millis();
        while (!Serial && (millis() - start_wait < 3000));
        delay(500); 

        Serial.println("\n\n=================================================");
        Serial.println("--- ESP32-C6 Antarctic Probe Firmware ---");
        Serial.println("SYSTEM START: Initial Power-On / Manual Reset");
        Serial.println("MISSION STATE: 0 (STANDBY)");
        Serial.println("=================================================");
        
        fram_init();
        fram_clear();
        record_counter = 1;
        session_start_time = 0;
        mission_state = STATE_STANDBY;
        
    } else {
        // Deep Sleep Resume Logic
        uint32_t start_wait = millis();
        while (!Serial && (millis() - start_wait < 500));
        
        Serial.println("\n\n--- ESP32-C6 Antarctic Probe Firmware ---");
        Serial.printf("Diagnostics -> State: %d, Reset: %d, Wakeup: %d\n", mission_state, (int)reset_reason, (int)wakeup_cause);
        fram_init();

        // STATE ROUTING
        if (mission_state == STATE_STANDBY) {
            if (is_button_wakeup) {
                Serial.println("[HITL] STATE 0 -> 1: PROBE ARMED. Sinking to depth...");
                mission_state = STATE_ARMED;
                session_start_time = millis();
            } else {
                Serial.println("[HITL] Spurious wakeup in STANDBY. Returning to sleep.");
            }
        } 
        else if (mission_state == STATE_ARMED) {
            Serial.printf("[HITL] STATE 1 (ARMED): Sinking interval delay (%ds)...\n", SINKING_DELAY_SEC);
            delay(SINKING_DELAY_SEC * 1000);
            Serial.println("[HITL] STATE 1 -> 2: Sinking delay complete. Starting LOGGING.");
            mission_state = STATE_LOGGING;
            log_sensor_data();
        } 
        else if (mission_state == STATE_LOGGING) {
            if (is_button_wakeup) {
                handle_offload();
                goto sleep_transition;
            } else {
                Serial.printf("[HITL] STATE 2 (LOGGING): Interval delay (%ds). Press D6 to offload.\n", LOGGING_INTERVAL_SEC);
                uint32_t wait_start = millis();
                while (millis() - wait_start < (LOGGING_INTERVAL_SEC * 1000)) {
                    if (digitalRead(REED_SWITCH_PIN) == LOW) {
                        handle_offload();
                        goto sleep_transition;
                    }
                    delay(10);
                }
                log_sensor_data();
            }
        }
    }

    sleep_transition:
    // --- SLEEP CONFIGURATION ---
    esp_sleep_enable_ext1_wakeup(1ULL << REED_SWITCH_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
    
    if (mission_state == STATE_STANDBY) {
        Serial.println("[HITL] Entering Indefinite Deep Sleep (Waiting for Arming)...");
    } else {
        int sleep_time = (mission_state == STATE_ARMED) ? SINKING_DELAY_SEC : LOGGING_INTERVAL_SEC;
        Serial.printf("[HITL] Entering Deep Sleep for %ds...\n", sleep_time);
        esp_sleep_enable_timer_wakeup((uint64_t)sleep_time * uS_TO_S_FACTOR);
    }

    Serial.flush();
    delay(1000); 
    esp_deep_sleep_start();
}

void loop() {}
