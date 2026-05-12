#include <Arduino.h>
#include "fram_manager.h"
#include "espnow_transfer.h"
#include "data_packet.h"

// ==============================================================================
// 1. HITL DEBUG CONFIGURATION & MISSION PARAMETERS
// ==============================================================================
#define DEBUG_MODE 1  // Set to 0 for production

#if DEBUG_MODE
    #define SINKING_DELAY_SEC 15  
    #define LOGGING_INTERVAL_SEC 5 
#else
    #define SINKING_DELAY_SEC 600 
    #define LOGGING_INTERVAL_SEC 300 
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
    Serial.println("[HITL] Waking up (Timer/Cycle)...");
    
    uint32_t current_time_ms = (record_counter - 1) * LOGGING_INTERVAL_SEC * 1000;
    String dummy_data = String(record_counter) + "," + String(current_time_ms) + ",1400.0,2200.0,100.0,105.0,90.0,110.0,95.0,100.0,120.0,80.0,90.0,110.0,105.0";
    
    Serial.println("[HITL] Saving to F-RAM: " + dummy_data);
    fram_write_record(dummy_data);
    record_counter++;
}

void setup() {
    // 1. Immediate hardware evaluation (Silent)
    esp_reset_reason_t reset_reason = esp_reset_reason();
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    pinMode(REED_SWITCH_PIN, INPUT_PULLUP);

    bool is_deepsleep_reset = (reset_reason == ESP_RST_DEEPSLEEP);
    bool is_button_wakeup = (wakeup_cause == ESP_SLEEP_WAKEUP_EXT1 || wakeup_cause == ESP_SLEEP_WAKEUP_GPIO);
    bool is_spurious = (is_deepsleep_reset && wakeup_cause == ESP_SLEEP_WAKEUP_UNDEFINED);

    // If we wake up from deep sleep but there is no valid trigger (USB noise) 
    // AND we are in STANDBY, we bypass the logging/serial logic entirely.
    if (!(is_spurious && mission_state == STATE_STANDBY)) {
        
        bool trigger_offload = false;
        bool skip_logging = false;

        // --- CLEAN PoC TIMING: Enforce delays BEFORE printing logs ---
        if (is_deepsleep_reset) {
            if (mission_state == STATE_ARMED && !is_button_wakeup) {
                delay(SINKING_DELAY_SEC * 1000);
                mission_state = STATE_LOGGING;
            } 
            else if (mission_state == STATE_LOGGING && !is_button_wakeup) {
                uint32_t wait_start = millis();
                while (millis() - wait_start < (LOGGING_INTERVAL_SEC * 1000)) {
                    if (digitalRead(REED_SWITCH_PIN) == LOW) {
                        trigger_offload = true;
                        break;
                    }
                    delay(10);
                }
            }
            else if (is_button_wakeup) {
                trigger_offload = true;
            }
        }

        // 2. Start Serial (After interval)
        Serial.begin(115200);
        uint32_t serial_wait = millis();
        if (!is_deepsleep_reset) {
            while (!Serial && (millis() - serial_wait < 3000));
            delay(500);
        } else {
            while (!Serial && (millis() - serial_wait < 500));
        }

        // 3. Print Logs
        Serial.println("\n\n--- ESP32-C6 Antarctic Probe Firmware ---");
        if (!is_deepsleep_reset) {
            Serial.println("SYSTEM START: Initial Power-On / Manual Reset");
            Serial.println("MISSION STATE: 0 (STANDBY)");
            fram_init();
            fram_clear();
            record_counter = 1;
            session_start_time = 0;
            mission_state = STATE_STANDBY;
        } else {
            Serial.printf("Diagnostics -> State: %d, Reset: %d, Wakeup: %d\n", mission_state, (int)reset_reason, (int)wakeup_cause);
            fram_init();

            if (trigger_offload) {
                if (mission_state == STATE_STANDBY) {
                    Serial.println("[HITL] STATE 0 -> 1: PROBE ARMED. Sinking to depth...");
                    mission_state = STATE_ARMED;
                    session_start_time = millis();
                } else if (mission_state == STATE_LOGGING) {
                    handle_offload();
                    skip_logging = true;
                }
            } else if (mission_state == STATE_LOGGING && !skip_logging) {
                log_sensor_data();
            }
        }
        
        if (mission_state == STATE_STANDBY) {
            Serial.println("[HITL] Entering Indefinite Deep Sleep (Waiting for Arming)...");
        } else {
            int sleep_time = (mission_state == STATE_ARMED) ? SINKING_DELAY_SEC : LOGGING_INTERVAL_SEC;
            Serial.printf("[HITL] Entering Deep Sleep for %ds...\n", sleep_time);
            esp_sleep_enable_timer_wakeup((uint64_t)sleep_time * uS_TO_S_FACTOR);
        }
    }

    // --- SLEEP CONFIGURATION ---
    esp_sleep_enable_ext1_wakeup(1ULL << REED_SWITCH_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
    Serial.flush();
    delay(1000); 
    esp_deep_sleep_start();
}

void loop() {}
