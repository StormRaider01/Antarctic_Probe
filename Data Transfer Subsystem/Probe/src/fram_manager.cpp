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

int fram_get_records(ProbeRecord_t* out_records, int max_records) {
    int count = 0;
    for (int i = 0; i < fram_record_count && count < max_records; i++) {
        uint32_t entry_num, time_ms;
        float temp, pressure;
        float spec[11];
        
        // Print raw string for HITL validation
        Serial.print("[HITL] RTC Memory Read [");
        Serial.print(i);
        Serial.print("]: ");
        Serial.println(simulated_fram[i]);

        // sscanf parses the 15-value string into the corresponding variables
        int parsed = sscanf(simulated_fram[i], "%u,%u,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f",
               &entry_num, &time_ms, &temp, &pressure,
               &spec[0], &spec[1], &spec[2], &spec[3], &spec[4], &spec[5],
               &spec[6], &spec[7], &spec[8], &spec[9], &spec[10]);
               
        if (parsed == 15) {
            out_records[count] = build_record(entry_num, time_ms, temp, pressure, spec);
            count++;
        } else {
            Serial.println("F-RAM Read Error: Failed to parse 15 values.");
        }
    }
    return count;
}

void fram_clear() {
    fram_record_count = 0;
    Serial.println("F-RAM Memory Cleared.");
}
