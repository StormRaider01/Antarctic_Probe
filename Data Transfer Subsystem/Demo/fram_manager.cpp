#include "fram_manager.h"

// Simulated F-RAM constraints
#define MAX_RECORDS 100
#define MAX_PAYLOAD_SIZE 128

// Use RTC_DATA_ATTR to retain variables across ESP32 Deep Sleep cycles
RTC_DATA_ATTR int fram_record_count = 0;
RTC_DATA_ATTR char simulated_fram[MAX_RECORDS][MAX_PAYLOAD_SIZE];

void fram_init() {
  Serial.println("F-RAM HAL: Initialised (Simulated via RTC Memory).");
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

int fram_get_records(ProbeRecord_t *out_records, int max_records) {
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

    // sscanf parses the 14-value string (CSV) into the corresponding binary variables.
    // Order: [0]reading, [1]time_ms, [2]temp_c, [3]depth_m, [4-13]SpecChannels (10 values).
    int parsed = sscanf(simulated_fram[i],
                        "%u,%u,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f",
                        &entry_num, &time_ms, &temp, &pressure, &spec[0],
                        &spec[1], &spec[2], &spec[3], &spec[4], &spec[5],
                        &spec[6], &spec[7], &spec[8], &spec[9]);

    if (parsed == 14) {
      // HANDOVER: Build the struct using the binary wire-format (data_packet.h)
      out_records[count] =
          build_record(entry_num, time_ms, temp, pressure,
                       spec[0], spec[1], spec[2], spec[3], spec[4],
                       spec[5], spec[6], spec[7], spec[8], spec[9]);
      count++;
    } else {
      Serial.println("F-RAM Read Error: Failed to parse 14 values.");
    }
  }
  return count;
}

void fram_clear() {
  fram_record_count = 0;
  Serial.println("F-RAM Memory Cleared.");
}
