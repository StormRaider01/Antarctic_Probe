#include <Arduino.h>
#include "fram_manager.h"

// Define Hardware Pins and Timers
#define REED_SWITCH_PIN GPIO_NUM_9
#define SLEEP_DURATION_SEC (5 * 60)
#define uS_TO_S_FACTOR 1000000ULL

// Simulated Record Number Tracker (survives deep sleep)
RTC_DATA_ATTR int record_counter = 1;

void setup() {
    // 1. Initialize Serial for debugging/offloading
    Serial.begin(115200);
    delay(100); // Allow serial to stabilize
    
    Serial.println("\n\n--- ESP32-C6 Antarctic Probe Firmware (SS2) ---");

    // 2. Initialize Hardware Abstraction Layers
    fram_init();

    // 3. Determine wake-up cause and route to the correct state
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        // --- STATE: WAKE_AND_LOG ---
        Serial.println("Wakeup Reason: TIMER. State -> WAKE_AND_LOG");
        
        // Generate a simulated 15-value data string
        String dummy_data = String(record_counter) + ",1777968000,1400,2200,100,105,90,110,95,100,120,80,90,110,105";
        
        fram_write_record(dummy_data);
        record_counter++;
        
    } else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 || wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
        // --- STATE: OFFLOAD ---
        Serial.println("Wakeup Reason: REED SWITCH (EXT). State -> OFFLOAD");
        
        // Dump all records from F-RAM
        fram_read_all();
        
        // Reset the record counter for the next deployment
        record_counter = 1;
    } else {
        // INITIAL BOOT
        Serial.println("Wakeup Reason: OTHER (Initial Boot/Reset).");
    }

    // --- STATE: DEEP_SLEEP ---
    Serial.println("State -> DEEP_SLEEP. Entering low-power mode...");
    
    // Configure hardware interrupt on GPIO9 (Reed Switch)
    pinMode(REED_SWITCH_PIN, INPUT_PULLUP);
    
    // Note: For ESP32-C6, GPIO wakeups use esp_deep_sleep_enable_gpio_wakeup or similar,
    // but ext0 is a common ESP32 wrapper. If ext0 fails on compilation, we'll swap it.
    esp_sleep_enable_ext0_wakeup(REED_SWITCH_PIN, 0); // Wake on logic LOW (magnet present)
    
    // Configure timer wake-up
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_SEC * uS_TO_S_FACTOR);

    // Flush serial and go to sleep immediately (No delay() blocking)
    Serial.flush();
    esp_deep_sleep_start();
}

void loop() {
    // Execution should never reach this block due to deep sleep
}
