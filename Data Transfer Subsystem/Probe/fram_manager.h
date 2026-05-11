#ifndef FRAM_MANAGER_H
#define FRAM_MANAGER_H

#include <Arduino.h>
#include "data_packet.h"

/**
 * @brief Initialize the F-RAM module.
 */
void fram_init();

/**
 * @brief Writes a 15-value comma-separated string payload to the F-RAM.
 */
void fram_write_record(String payload);

/**
 * @brief Parses all simulated strings into an array of ProbeRecord_t.
 * @param out_records Array to fill
 * @param max_records Capacity of out_records
 * @return Number of records populated
 */
int fram_get_records(ProbeRecord_t* out_records, int max_records);

/**
 * @brief Clears simulated memory.
 */
void fram_clear();

#endif
