#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>

#include <lvgl.h>

#include <zmk/event_manager.h>

#include "events/swipe_gesture_event.h"
#include "touch_handler.h"

LOG_MODULE_REGISTER(prospector_touch, CONFIG_ZMK_LOG_LEVEL);

#define TOUCH_NODE DT_NODELABEL(lcd_touch)

#if !DT_NODE_EXISTS(TOUCH_NODE)
#error "lcd_touch node not found"
#endif

#define PANEL_WIDTH 240
#define PANEL_HEIGHT 280
#define DISPLAY_WIDTH 280
#define DISPLAY_HEIGHT 240

#define SWIPE_THRESHOLD 30
#define SWIPE_COOLDOWN_MS 400
#define DOUBLE_TAP_THRESHOLD 10
#define DOUBLE_TAP_INTERVAL_MS 350

__attribute__((weak)) bool prospector_touch_ui_is_interacting(void) { return false; }

static uint16_t current_x;
static uint16_t current_y;
static bool touch_active;
static bool prev_touch_active;

static lv_indev_t *lvgl_indev;

static struct {
    int32_t start_x;
    int32_t start_y;
    bool in_progress;
} swipe_state;

static int64_t last_swipe_time;
static int64_t last_tap_time;

static void panel_to_display(uint16_t px, uint16_t py, int32_t *dx, int32_t *dy) {
#if defined(CONFIG_PROSPECTOR_TOUCH_ROTATION_90)
    int32_t x = py;
    int32_t y = (PANEL_WIDTH - 1) - px;
#else
    int32_t x = (PANEL_HEIGHT - 1) - py;
    int32_t y = px;
#endif

#if IS_ENABLED(CONFIG_PROSPECTOR_TOUCH_INVERT_X)
    x = (DISPLAY_WIDTH - 1) - x;
#endif
#if IS_ENABLED(CONFIG_PROSPECTOR_TOUCH_INVERT_Y)
    y = (DISPLAY_HEIGHT - 1) - y;
#endif

    *dx = x;
    *dy = y;
}

static void raise_swipe_event(enum swipe_direction direction) {
    int64_t now = k_uptime_get();

    if ((now - last_swipe_time) < SWIPE_COOLDOWN_MS) {
        return;
    }

    if (prospector_touch_ui_is_interacting()) {
        return;
    }

    last_swipe_time = now;
    raise_zmk_swipe_gesture_event((struct zmk_swipe_gesture_event){.direction = direction});
}

static void touch_input_callback(struct input_event *evt, void *user_data) {
    ARG_UNUSED(user_data);

    LOG_DBG("input type=%d code=%d value=%d", evt->type, evt->code, evt->value);

    switch (evt->code) {
    case INPUT_ABS_X:
        current_x = (uint16_t)evt->value;
        break;
    case INPUT_ABS_Y:
        current_y = (uint16_t)evt->value;
        break;
    case INPUT_BTN_TOUCH: {
        touch_active = (evt->value != 0);

        int32_t display_x;
        int32_t display_y;
        panel_to_display(current_x, current_y, &display_x, &display_y);

        bool touch_started = touch_active && !prev_touch_active;

        if (touch_started) {
            swipe_state.start_x = display_x;
            swipe_state.start_y = display_y;
            swipe_state.in_progress = true;
        } else if (!touch_active && swipe_state.in_progress) {
            int32_t dx = display_x - swipe_state.start_x;
            int32_t dy = display_y - swipe_state.start_y;
            int32_t abs_dx = dx < 0 ? -dx : dx;
            int32_t abs_dy = dy < 0 ? -dy : dy;

            if (abs_dy > abs_dx && abs_dy > SWIPE_THRESHOLD) {
                raise_swipe_event(dy > 0 ? SWIPE_DIRECTION_DOWN : SWIPE_DIRECTION_UP);
            } else if (abs_dx > abs_dy && abs_dx > SWIPE_THRESHOLD) {
                raise_swipe_event(dx > 0 ? SWIPE_DIRECTION_RIGHT : SWIPE_DIRECTION_LEFT);
            } else if (abs_dx <= DOUBLE_TAP_THRESHOLD && abs_dy <= DOUBLE_TAP_THRESHOLD) {
                int64_t now = k_uptime_get();
                if (last_tap_time > 0 && (now - last_tap_time) < DOUBLE_TAP_INTERVAL_MS) {
                    last_tap_time = 0;
                    raise_swipe_event(SWIPE_DIRECTION_DOUBLE_TAP);
                } else {
                    last_tap_time = now;
                }
            }

            swipe_state.in_progress = false;
        }

        prev_touch_active = touch_active;
        break;
    }
    default:
        break;
    }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(TOUCH_NODE), touch_input_callback, NULL);

#if IS_ENABLED(CONFIG_PROSPECTOR_TOUCH_DEBUG)
static void touch_debug_timer_cb(struct k_timer *timer) {
    ARG_UNUSED(timer);
    LOG_INF("touch alive active=%d x=%u y=%u", touch_active, current_x, current_y);
}

K_TIMER_DEFINE(touch_debug_timer, touch_debug_timer_cb, NULL);

static int touch_debug_init(void) {
    const struct device *dev = DEVICE_DT_GET(TOUCH_NODE);
    LOG_INF("CST816S device ready=%d", device_is_ready(dev));
    k_timer_start(&touch_debug_timer, K_SECONDS(2), K_SECONDS(2));
    return 0;
}

SYS_INIT(touch_debug_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif

static void lvgl_input_read(lv_indev_t *indev, lv_indev_data_t *data) {
    ARG_UNUSED(indev);

    int32_t display_x;
    int32_t display_y;
    panel_to_display(current_x, current_y, &display_x, &display_y);

    if (display_x < 0) {
        display_x = 0;
    } else if (display_x >= DISPLAY_WIDTH) {
        display_x = DISPLAY_WIDTH - 1;
    }

    if (display_y < 0) {
        display_y = 0;
    } else if (display_y >= DISPLAY_HEIGHT) {
        display_y = DISPLAY_HEIGHT - 1;
    }

    data->point.x = (lv_coord_t)display_x;
    data->point.y = (lv_coord_t)display_y;
    data->state = touch_active ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool touch_handler_device_ready(void) {
    return device_is_ready(DEVICE_DT_GET(TOUCH_NODE));
}

void touch_handler_get_point(uint16_t *x, uint16_t *y, bool *active) {
    if (x != NULL) {
        *x = current_x;
    }
    if (y != NULL) {
        *y = current_y;
    }
    if (active != NULL) {
        *active = touch_active;
    }
}

int touch_handler_register_lvgl_indev(void) {
    if (lvgl_indev != NULL) {
        return 0;
    }

    lvgl_indev = lv_indev_create();
    if (lvgl_indev == NULL) {
        return -ENOMEM;
    }

    lv_indev_set_type(lvgl_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_indev, lvgl_input_read);

    return 0;
}
