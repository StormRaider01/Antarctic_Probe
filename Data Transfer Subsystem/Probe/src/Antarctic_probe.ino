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
// NOTE: On ESP32-C6 FireBeetle 2, LP-IOs (RTC IOs) are GPIO 0-7.
// D6 is GPIO 1. For HITL testing, wire your external push button between D6 and GND.
#define REED_SWITCH_PIN GPIO_NUM_1  
#define uS_TO_S_FACTOR 1000000ULL

// Simulated Record Number Tracker (survives deep sleep/reset)
RTC_DATA_ATTR int record_counter = 1;
RTC_DATA_ATTR uint32_t session_start_time = 0; // ms

void setup() {
    Serial.begin(115200);
    
    // Wait for Serial to initialize
    uint32_t start_wait = millis();
    while (!Serial && (millis() - start_wait < 4000));
    delay(500); 
    
    Serial.println("\n\n--- ESP32-C6 Antarctic Probe Firmware ---");
    
    // Diagnostic: Check Reset Reason
    esp_reset_reason_t reset_reason = esp_reset_reason();
    Serial.printf("Reset Reason: %d ", reset_reason);
    switch (reset_reason) {
        case ESP_RST_POWERON: Serial.println("(Power-on)"); break;
        case ESP_RST_SW:      Serial.println("(Software Reset)"); break;
        case ESP_RST_DEEPSLEEP: Serial.println("(Deep Sleep Wakeup)"); break;
        case ESP_RST_PANIC:   Serial.println("(Panic/Crash)"); break;
        default:              Serial.println("(Other)"); break;
    }

    #if DEBUG_MODE
    Serial.println("[HITL] DEBUG_MODE ACTIVE (Timer: 5s, Trigger: D6/GPIO 1)");
    Serial.println("[HITL] WIRING: Connect button between D6 (GPIO 1) and GND.");
    #endif

    fram_init();

    // Determine wake-up cause
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        // --- STATE: WAKE_AND_LOG ---
        #if DEBUG_MODE
        Serial.println("[HITL] Waking up (Timer)...");
        #endif
        
        uint32_t current_time_ms = (record_counter - 1) * SLEEP_DURATION_SEC * 1000;
        String dummy_data = String(record_counter) + "," + String(current_time_ms) + ",1400.0,2200.0,100.0,105.0,90.0,110.0,95.0,100.0,120.0,80.0,90.0,110.0,105.0";
        
        #if DEBUG_MODE
        Serial.println("[HITL] Saving to F-RAM: " + dummy_data);
        #endif
        
        fram_write_record(dummy_data);
        record_counter++;
        
    } else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1 || wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
        // --- STATE: OFFLOAD ---
        #if DEBUG_MODE
        Serial.println("[HITL] WAKEUP CAUSE: GPIO 1 (Magnetic Switch Simulated)!");
        Serial.println("[HITL] Offloading F-RAM contents...");
        #endif
        
        ProbeRecord_t records[100];
        int count = fram_get_records(records, 100);
        
        if (count > 0) {
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
        session_start_time = millis(); 
        
    } else {
        // INITIAL BOOT / MANUAL RESET
        Serial.println("System Start: (Initial Boot or Manual Reset).");
        // We do NOT clear F-RAM here anymore, so it survives software resets/loops
        if (session_start_time == 0) session_start_time = millis();
    }

    // --- STATE: DEEP_SLEEP ---
    #if DEBUG_MODE
    Serial.printf("[HITL] Entering DEEP SLEEP for %ds...\n", SLEEP_DURATION_SEC);
    #else
    Serial.println("State -> DEEP_SLEEP. Entering low-power mode...");
    #endif
    
    // Configure wake-up sources
    pinMode(REED_SWITCH_PIN, INPUT_PULLUP);
    
    // Ensure RTC peripherals stay powered to handle the pull-up and wakeup
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    
    esp_sleep_enable_ext1_wakeup(1ULL << REED_SWITCH_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_SEC * uS_TO_S_FACTOR);

    Serial.flush();
    delay(200); 
    esp_deep_sleep_start();
    
    // If we reach here, sleep failed
    Serial.println("FATAL: Deep sleep failed to start!");
    while(1) { delay(1000); }
}

void loop() {}
