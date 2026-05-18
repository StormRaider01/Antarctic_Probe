#ifndef FRAM_MANAGER_H
#define FRAM_MANAGER_H

#include <Arduino.h>
#include "data_packet.h"

void fram_init();
void fram_write_record(String payload);
int  fram_get_records(ProbeRecord_t* out_records, int max_records);
void fram_clear();

#endif
