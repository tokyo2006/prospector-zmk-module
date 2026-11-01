#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

enum swipe_direction {
    SWIPE_DIRECTION_NONE = -1,
    SWIPE_DIRECTION_UP = 0,
    SWIPE_DIRECTION_DOWN = 1,
    SWIPE_DIRECTION_LEFT = 2,
    SWIPE_DIRECTION_RIGHT = 3,
    SWIPE_DIRECTION_DOUBLE_TAP = 4,
};

struct zmk_swipe_gesture_event {
    enum swipe_direction direction;
};

ZMK_EVENT_DECLARE(zmk_swipe_gesture_event);
