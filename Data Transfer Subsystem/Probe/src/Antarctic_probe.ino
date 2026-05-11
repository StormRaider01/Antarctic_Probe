#include "espnow_transfer.h"
#include "fram_manager.h"
#include <Arduino.h>

// ==============================================================================
// 1. HITL DEBUG CONFIGURATION
// ==============================================================================
#define DEBUG_MODE 1 // Set to 0 for production

#if DEBUG_MODE
#define SLEEP_DURATION_SEC 5 // Accelerated 5s for HITL testing
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define SLEEP_DURATION_SEC (5 * 60) // 5 minutes for production
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(...)
#endif

#define REED_SWITCH_PIN GPIO_NUM_0 // Changed from GPIO 9 (BOOT) to GPIO 0 (LP IO)
#define uS_TO_S_FACTOR 1000000ULL

// Simulated Record Number Tracker (survives deep sleep/reset)
RTC_DATA_ATTR int record_counter = 1;
RTC_DATA_ATTR uint32_t session_start_time = 0; // ms

void setup() {
  Serial.begin(115200);

  // Wait for Serial to initialize (crucial for C6 USB-Serial re-enumeration)
  uint32_t start_wait = millis();
  while (!Serial && (millis() - start_wait < 5000))
    ;
  delay(1000);

  Serial.println("\n\n--- ESP32-C6 Antarctic Probe Firmware ---");
#if DEBUG_MODE
  Serial.println("[HITL] DEBUG_MODE ACTIVE (Timer: 5s, Pin: GPIO 0)");
  Serial.println("[HITL] NOTE: Wire your button between GPIO 0 and GND.");
#endif

  fram_init();

  // Determine wake-up cause
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
// --- STATE: WAKE_AND_LOG ---
#if DEBUG_MODE
    Serial.println("[HITL] Waking up (Timer)...");
#else
    Serial.println("Wakeup Reason: TIMER. State -> WAKE_AND_LOG");
#endif

    uint32_t current_time_ms = (record_counter - 1) * SLEEP_DURATION_SEC * 1000;

    // Generate dummy reading
    String dummy_data = String(record_counter) + "," + String(current_time_ms) +
                        ",1400.0,2200.0,100.0,105.0,90.0,110.0,95.0,100.0,120."
                        "0,80.0,90.0,110.0,105.0";

#if DEBUG_MODE
    Serial.println("[HITL] Saving to F-RAM: " + dummy_data);
#endif

    fram_write_record(dummy_data);
    record_counter++;

  } else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
// --- STATE: OFFLOAD ---
#if DEBUG_MODE
    Serial.println("[HITL] WAKEUP CAUSE: GPIO 0 (Magnetic Switch Simulated)!");
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
    Serial.println("Wakeup Reason: OTHER (Initial Boot/Reset).");
    session_start_time = millis();
    record_counter = 1;
    fram_clear();
  }

  // --- STATE: DEEP_SLEEP ---
  pinMode(REED_SWITCH_PIN, INPUT_PULLUP);

  Serial.printf("[HITL] Entering True Deep Sleep for %ds...\n",
                SLEEP_DURATION_SEC);
  Serial.flush();

  // Configure wake-up sources
  // GPIO 0 is an LP IO on C6, which supports ext1 wakeup in deep sleep.
  esp_sleep_enable_ext1_wakeup(1ULL << REED_SWITCH_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_SEC * uS_TO_S_FACTOR);

  esp_deep_sleep_start();
}

void loop() {}

void loop() {}
