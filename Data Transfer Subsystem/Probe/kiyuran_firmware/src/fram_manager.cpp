#include "fram_manager.h"

// Simulated F-RAM constraints
#define MAX_RECORDS 100
#define MAX_PAYLOAD_SIZE 55

// Use RTC_DATA_ATTR to retain variables across ESP32 Deep Sleep cycles
RTC_DATA_ATTR int fram_record_count = 0;
RTC_DATA_ATTR char simulated_fram[MAX_RECORDS][MAX_PAYLOAD_SIZE];

void fram_init() {
    Serial.println("F-RAM HAL: Initialized (Simulated via RTC Memory).");
}

void fram_write_record(String payload) {
    if (fram_record_count < MAX_RECORDS) {
        // Convert the String to a char array and save it to the RTC memory block
        payload.toCharArray(simulated_fram[fram_record_count], MAX_PAYLOAD_SIZE);
        Serial.print("F-RAM Write [Record ");
        Serial.print(fram_record_count);
        Serial.print("]: ");
        Serial.println(simulated_fram[fram_record_count]);
        fram_record_count++;
    } else {
        Serial.println("F-RAM Write Error: Memory Full!");
    }
}

void fram_read_all() {
    Serial.println("--- F-RAM Data Dump Started ---");
    for (int i = 0; i < fram_record_count; i++) {
        // Output each stored string, which will be intercepted by the ESP-NOW logic later
        Serial.println(simulated_fram[i]);
    }
    Serial.println("--- F-RAM Data Dump Complete ---");
    
    // Clear simulated memory after offloading
    fram_record_count = 0;
}
