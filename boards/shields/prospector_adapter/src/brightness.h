#pragma once

#include <stdbool.h>
#include <stdint.h>

bool prospector_brightness_get_auto(void);
uint8_t prospector_brightness_get_manual(void);
void prospector_brightness_set_auto(bool enabled);
void prospector_brightness_set_manual(uint8_t level);
