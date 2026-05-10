#ifndef FRAM_MANAGER_H
#define FRAM_MANAGER_H

#include <Arduino.h>

/**
 * @brief Initialize the F-RAM module.
 * In this simulated version, it prints initialization status.
 */
void fram_init();

/**
 * @brief Writes a 15-value comma-separated string payload to the F-RAM.
 * In this simulation, it writes to RTC memory.
 * @param payload The data string to save.
 */
void fram_write_record(String payload);

/**
 * @brief Reads all stored records from F-RAM and prints them to Serial.
 * Afterwards, it clears the simulated memory space.
 */
void fram_read_all();

#endif
