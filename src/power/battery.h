#pragma once
#include <stdint.h>
#include <stdbool.h>

void    battery_init();
float   battery_read_voltage();
uint8_t battery_get_percent();
uint8_t battery_v_to_percent(float v);
bool    battery_is_low_brownout(float v);
