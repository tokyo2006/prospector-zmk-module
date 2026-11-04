#pragma once

#include <stdbool.h>

int touch_handler_register_lvgl_indev(void);
bool prospector_touch_ui_is_interacting(void);

bool touch_handler_device_ready(void);
void touch_handler_get_point(uint16_t *x, uint16_t *y, bool *active);
