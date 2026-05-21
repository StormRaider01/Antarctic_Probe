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
RTC_DATA_ATTR uint32_t global_session_date = 20260515; // YYYYMMDD
RTC_DATA_ATTR char global_session_time[10] = "00:00:00";

/**
 * Mock battery level function.
 */
int get_battery_level() {
    return 70; // Hardcoded as requested
}

/**
 * Handles a command session over ESP-NOW.
 * Returns true if a mission-critical command was processed (PREPARE or RETRIEVE).
 */
bool handle_command_session() {
    Serial.println("[CMD] Entering Command Session. Waiting for CONNECT...");

    if (ESPNOW_Init() != ESPNOW_OK) {
        Serial.println("[CMD] ESP-NOW Init failed.");
        return false;
    }

    bool session_active = true;
    bool action_performed = false;
    uint32_t last_activity = millis();
    const uint32_t TIMEOUT_MS = 60000; // 1 minute timeout

    while (session_active && (millis() - last_activity < TIMEOUT_MS)) {
        char cmd_buf[64];
        if (ESPNOW_GetCommand(cmd_buf, sizeof(cmd_buf))) {
            last_activity = millis();
            String cmd = String(cmd_buf);
            Serial.printf("[CMD] Received: %s\n", cmd_buf);

            if (cmd == "[CMD]:CONNECT") {
                char resp[32];
                snprintf(resp, sizeof(resp), "[ACK]:CONNECT,BATT:%d", get_battery_level());
                ESPNOW_SendString(resp);
                Serial.println("[CMD] Sent CONNECT ACK with battery info.");
            }
            else if (cmd.startsWith("[CMD]:PREPARE")) {
                // Format: [CMD]:PREPARE, 2026-05-13, 12:15:00
                if (cmd.length() >= 29) {
                    String dateStr = cmd.substring(14, 24); // "2026-05-13"
                    String timeStr = cmd.substring(25);     // "12:15:00"

                    // Convert date to YYYYMMDD
                    String y = dateStr.substring(0, 4);
                    String m = dateStr.substring(5, 7);
                    String d = dateStr.substring(8, 10);
                    global_session_date = (y.toInt() * 10000) + (m.toInt() * 100) + d.toInt();

                    strncpy(global_session_time, timeStr.c_str(), sizeof(global_session_time) - 1);

                    Serial.printf("[CMD] Prepared mission: Date=%lu, Time=%s\n", global_session_date, global_session_time);
                    ESPNOW_SendString("[ACK]:PREPARE");
                    action_performed = true;
                    session_active = false; // Exit after prepare to start mission
                }
            }
            else if (cmd == "[CMD]:RETRIEVE") {
                ESPNOW_SendString("[ACK]:RETRIEVE");
                // The actual transfer happens outside this loop or we call it here
                action_performed = true;
                session_active = false;
            }
            else if (cmd == "[CMD]:DISCONNECT") {
                ESPNOW_SendString("[ACK]:DISCONNECT");
                session_active = false;
            }
        }
        delay(10);
    }

    if (millis() - last_activity >= TIMEOUT_MS) {
        Serial.println("[CMD] Session timed out.");
    }

    ESPNOW_Deinit();
    return action_performed;
}

/**
 * Executes the data offload sequence using ESP-NOW.
 */
void handle_offload() {
    Serial.println("\n[HITL] WAKEUP CAUSE: GPIO 1 (Magnetic Switch Simulated)!");

    // 1. Enter Command Session to wait for CONNECT/RETRIEVE
    if (!handle_command_session()) {
        Serial.println("[OFFLOAD] No command received or session failed.");
        return;
    }

    // 2. If we reach here, a command was processed.
    // Note: handle_command_session de-inits ESPNOW, so we need to re-init if we start transfer.
    // Or we could have handle_command_session NOT de-init if RETRIEVE was called.
    // Let's assume RETRIEVE was the action performed.

    ProbeRecord_t* records = (ProbeRecord_t*)malloc(sizeof(ProbeRecord_t) * 100);
    if (records != NULL) {
        int count = fram_get_records(records, 100);
        if (count > 0) {
            if (ESPNOW_Init() == ESPNOW_OK) {
                ESPNOW_StartTransfer(records, count, session_start_time, global_session_date);
                ESPNOW_Deinit();
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
    Serial.flush();
}

/**
 * Generates a dummy sensor reading (11-step PWM ramp) and saves it to simulated F-RAM.
 */
void log_sensor_data() {
    Serial.println("[HITL] Waking up (Timer/Cycle) -> Generating 11-step LED PWM ramp...");
    
    uint32_t base_time_ms = (record_counter - 1) * LOGGING_INTERVAL_SEC * 1000;
    
    for (int step = 0; step < 11; step++) {
        uint32_t current_time_ms = base_time_ms + (step * 116); // ~116ms per step
        float temp_c = 20.50;
        float depth_m = 0.100;
        
        // Base values
        float F1 = 15.0 * step;
        float F2 = 350.0 * step; // Excitation ramps up
        float F3 = 200.0 * step;
        float F4 = 18.0 * step;
        float F5 = 10.0 * step;
        float F6 = 8.0 * step;
        float F7 = 13.0 * step;
        float F8 = 25.0 * step;
        float clear = 300.0 * step;
        float NIR = 8.0 * step;

        String dummy_data = String(record_counter) + "," + String(current_time_ms) + "," + 
                            String(temp_c, 2) + "," + String(depth_m, 3) + "," + 
                            String(F1, 0) + "," + String(F2, 0) + "," + String(F3, 0) + "," + 
                            String(F4, 0) + "," + String(F5, 0) + "," + String(F6, 0) + "," + 
                            String(F7, 0) + "," + String(F8, 0) + "," + String(clear, 0) + "," + String(NIR, 0);
                            
        // Serial.println("[HITL] Saving to F-RAM: " + dummy_data); // Suppress to avoid spam, fram_write_record prints it
        fram_write_record(dummy_data);
    }
    
    record_counter++;
    Serial.flush();
}

void setup() {
    // 1. Evaluate wakeup cause immediately
    esp_reset_reason_t reset_reason = esp_reset_reason();
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    pinMode(REED_SWITCH_PIN, INPUT_PULLUP);

    bool is_deepsleep = (reset_reason == ESP_RST_DEEPSLEEP);
    bool is_button = (wakeup_cause == ESP_SLEEP_WAKEUP_EXT1 || wakeup_cause == ESP_SLEEP_WAKEUP_GPIO);
    bool is_spurious = (is_deepsleep && wakeup_cause == ESP_SLEEP_WAKEUP_UNDEFINED);

    // 2. Robust Serial Initialisation
    Serial.begin(115200);
    uint32_t wait_start = millis();
    // Give the USB-CDC plenty of time to stabilize on the host
    while (!Serial && (millis() - wait_start < 2000));
    delay(500);

    // --- SPURIOUS WAKEUP SUPPRESSION ---
    // If it's just noise, go back to sleep with minimal chatter
    if (is_spurious && mission_state == STATE_STANDBY) {
        // We print a single dot to show the chip is alive but looping
        Serial.print(".");
        goto sleep_transition;
    }

    Serial.println("\n\n--- ESP32-C6 Antarctic Probe Firmware ---");

    if (!is_deepsleep) {
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

        // --- STATE 0: STANDBY ---
        if (mission_state == STATE_STANDBY) {
            if (is_button) {
                Serial.println("[HITL] STATE 0 Triggered: Entering PRE-DEPLOYMENT Command Session...");
                if (handle_command_session()) {
                    Serial.println("[HITL] PREPARE Complete. Mission ARMED. Sinking...");
                    mission_state = STATE_ARMED;
                    session_start_time = millis();
                    Serial.flush();
                    delay(1000);
                } else {
                    Serial.println("[HITL] Command session aborted. Staying in STANDBY.");
                }
            }
        }
        // --- STATE 1: ARMED (Sinking) ---
        else if (mission_state == STATE_ARMED) {
            if (is_button) {
                // If button pressed during ARMED, jump straight to logging
                Serial.println("[HITL] Manual Trigger: Bypassing sinking delay.");
                mission_state = STATE_LOGGING;
                log_sensor_data();
            } else {
                Serial.printf("[HITL] STATE 1 (ARMED): Sinking interval wait (%ds)...\n", SINKING_DELAY_SEC);
                Serial.flush();
                // Real-time polling wait
                uint32_t sinking_start = millis();
                while (millis() - sinking_start < (SINKING_DELAY_SEC * 1000)) {
                    if (digitalRead(REED_SWITCH_PIN) == LOW) {
                        Serial.println("[HITL] Sinking interrupted by manual trigger.");
                        break;
                    }
                    delay(10);
                }
                Serial.println("[HITL] STATE 1 -> 2: Sinking delay complete. Starting LOGGING.");
                mission_state = STATE_LOGGING;
                log_sensor_data();
            }
        }
        // --- STATE 2: LOGGING ---
        else if (mission_state == STATE_LOGGING) {
            if (is_button) {
                handle_offload();
            } else {
                Serial.printf("[HITL] STATE 2 (LOGGING): Cycle wait (%ds). Press D6 to offload.\n", LOGGING_INTERVAL_SEC);
                Serial.flush();
                uint32_t logging_start = millis();
                bool triggered = false;
                while (millis() - logging_start < (LOGGING_INTERVAL_SEC * 1000)) {
                    if (digitalRead(REED_SWITCH_PIN) == LOW) {
                        handle_offload();
                        triggered = true;
                        break;
                    }
                    delay(10);
                }
                if (!triggered) log_sensor_data();
            }
        }
    }

    sleep_transition:
    // --- FINAL SLEEP PREPARATION ---
    esp_sleep_enable_ext1_wakeup(1ULL << REED_SWITCH_PIN, ESP_EXT1_WAKEUP_ANY_LOW);

    if (mission_state == STATE_STANDBY) {
        if (!is_spurious) Serial.println("[HITL] Entering Indefinite Deep Sleep (Waiting for Arming)...");
    } else {
        int sleep_time = (mission_state == STATE_ARMED) ? SINKING_DELAY_SEC : LOGGING_INTERVAL_SEC;
        Serial.printf("[HITL] Entering Deep Sleep for %ds...\n", sleep_time);
        esp_sleep_enable_timer_wakeup((uint64_t)sleep_time * uS_TO_S_FACTOR);
    }

    Serial.flush();
    delay(1000); // CRITICAL: Stability delay for USB-CDC PHY shutdown
    esp_deep_sleep_start();
}

void loop() {}
