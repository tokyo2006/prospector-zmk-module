#pragma once

#include <zephyr/devicetree.h>

/* The panel is rotated 90 degrees, so its native height is the LVGL width.
 * 320-wide panels (172x320) use a shorter, wider layout than the stock
 * 280x240 Prospector panel. */
#if DT_PROP(DT_CHOSEN(zephyr_display), height) >= 320
#define OPERATOR_CONTENT_WIDTH 300
#define OPERATOR_WPM_HEIGHT    70
#define OPERATOR_LAYER_Y       122
#define OPERATOR_BOTTOM_Y      110
#define OPERATOR_OUTPUT_X      198
#else
#define OPERATOR_CONTENT_WIDTH 260
#define OPERATOR_WPM_HEIGHT    90
#define OPERATOR_LAYER_Y       142
#define OPERATOR_BOTTOM_Y      170
#define OPERATOR_OUTPUT_X      148
#endif
