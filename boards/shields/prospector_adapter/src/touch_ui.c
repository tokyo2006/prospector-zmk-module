#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>

#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>

#include "brightness.h"
#include "events/swipe_gesture_event.h"
#include "touch_handler.h"

LOG_MODULE_DECLARE(prospector_touch, CONFIG_ZMK_LOG_LEVEL);

enum overlay_kind {
    OVERLAY_NONE,
    OVERLAY_SETTINGS,
    OVERLAY_QUICK,
};

static lv_obj_t *overlay;
static enum overlay_kind overlay_kind;
static volatile enum swipe_direction pending_swipe = SWIPE_DIRECTION_NONE;
static bool indev_registered;
static bool interaction_active;

static lv_obj_t *settings_slider;
static lv_obj_t *settings_value;
static lv_obj_t *settings_auto_switch;

bool prospector_touch_ui_is_interacting(void) { return interaction_active; }

static void close_overlay(void) {
    if (overlay != NULL) {
        lv_obj_del(overlay);
        overlay = NULL;
    }

    overlay_kind = OVERLAY_NONE;
    settings_slider = NULL;
    settings_value = NULL;
    settings_auto_switch = NULL;
    interaction_active = false;
}

static void close_btn_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        close_overlay();
    }
}

static lv_obj_t *create_overlay_base(const char *title) {
    overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, 280, 240);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title_label = lv_label_create(overlay);
    lv_label_set_text(title_label, title);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *close_btn = lv_btn_create(overlay);
    lv_obj_set_size(close_btn, 40, 32);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_add_event_cb(close_btn, close_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_center(close_label);

    return overlay;
}

static void update_settings_value_label(void) {
    if (settings_value == NULL) {
        return;
    }

    if (prospector_brightness_get_auto()) {
        lv_label_set_text(settings_value, "Auto");
    } else {
        lv_label_set_text_fmt(settings_value, "%u%%", prospector_brightness_get_manual());
    }
}

static void settings_slider_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *slider = lv_event_get_target(e);

    if (code == LV_EVENT_PRESSED) {
        interaction_active = true;
    } else if (code == LV_EVENT_RELEASED) {
        interaction_active = false;
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        prospector_brightness_set_manual((uint8_t)lv_slider_get_value(slider));
        update_settings_value_label();
    }
}

static void settings_auto_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    lv_obj_t *sw = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);

    prospector_brightness_set_auto(enabled);

    if (settings_slider != NULL) {
        if (enabled) {
            lv_obj_add_state(settings_slider, LV_STATE_DISABLED);
            lv_obj_set_style_opa(settings_slider, LV_OPA_50, 0);
        } else {
            lv_obj_clear_state(settings_slider, LV_STATE_DISABLED);
            lv_obj_set_style_opa(settings_slider, LV_OPA_COVER, 0);
        }
    }

    update_settings_value_label();
}

static void create_settings_overlay(void) {
    create_overlay_base("Display Settings");

    lv_obj_t *brightness_label = lv_label_create(overlay);
    lv_label_set_text(brightness_label, "Brightness");
    lv_obj_set_pos(brightness_label, 20, 55);

    settings_value = lv_label_create(overlay);
    lv_obj_align(settings_value, LV_ALIGN_TOP_RIGHT, -20, 55);
    update_settings_value_label();

    settings_slider = lv_slider_create(overlay);
    lv_obj_set_size(settings_slider, 200, 20);
    lv_obj_set_pos(settings_slider, 20, 85);
    lv_slider_set_range(settings_slider, 1, 100);
    lv_slider_set_value(settings_slider, prospector_brightness_get_manual(), LV_ANIM_OFF);
    lv_obj_add_event_cb(settings_slider, settings_slider_event_cb, LV_EVENT_ALL, NULL);

    if (prospector_brightness_get_auto()) {
        lv_obj_add_state(settings_slider, LV_STATE_DISABLED);
        lv_obj_set_style_opa(settings_slider, LV_OPA_50, 0);
    }

    if (IS_ENABLED(CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR)) {
        lv_obj_t *auto_label = lv_label_create(overlay);
        lv_label_set_text(auto_label, "Auto");
        lv_obj_set_pos(auto_label, 20, 120);

        settings_auto_switch = lv_switch_create(overlay);
        lv_obj_set_size(settings_auto_switch, 50, 28);
        lv_obj_set_pos(settings_auto_switch, 220, 116);
        if (prospector_brightness_get_auto()) {
            lv_obj_add_state(settings_auto_switch, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(settings_auto_switch, settings_auto_event_cb,
                            LV_EVENT_VALUE_CHANGED, NULL);
    }

    lv_obj_t *hint = lv_label_create(overlay);
    lv_label_set_text(hint, "Swipe up to close");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    overlay_kind = OVERLAY_SETTINGS;
}

static void bootloader_btn_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        sys_reboot(0x57);
    }
}

static void reset_btn_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        sys_reboot(SYS_REBOOT_WARM);
    }
}

static void create_quick_overlay(void) {
    create_overlay_base("Quick Actions");

    lv_obj_t *bootloader_btn = lv_btn_create(overlay);
    lv_obj_set_size(bootloader_btn, 200, 50);
    lv_obj_align(bootloader_btn, LV_ALIGN_CENTER, 0, -40);
    lv_obj_add_event_cb(bootloader_btn, bootloader_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bootloader_label = lv_label_create(bootloader_btn);
    lv_label_set_text(bootloader_label, "Enter Bootloader");
    lv_obj_center(bootloader_label);

    lv_obj_t *reset_btn = lv_btn_create(overlay);
    lv_obj_set_size(reset_btn, 200, 50);
    lv_obj_align(reset_btn, LV_ALIGN_CENTER, 0, 30);
    lv_obj_add_event_cb(reset_btn, reset_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reset_label = lv_label_create(reset_btn);
    lv_label_set_text(reset_label, "System Reset");
    lv_obj_center(reset_label);

    lv_obj_t *hint = lv_label_create(overlay);
    lv_label_set_text(hint, "Swipe left to close");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    overlay_kind = OVERLAY_QUICK;
}

static void overlay_work_cb(struct k_work *work) {
    ARG_UNUSED(work);

    if (!zmk_display_is_initialized()) {
        return;
    }

    if (!indev_registered) {
        if (touch_handler_register_lvgl_indev() == 0) {
            indev_registered = true;
        }
    }

    enum swipe_direction direction = pending_swipe;
    pending_swipe = SWIPE_DIRECTION_NONE;

    if (direction == SWIPE_DIRECTION_NONE) {
        return;
    }

    if (overlay == NULL) {
        if (direction == SWIPE_DIRECTION_DOWN) {
            create_settings_overlay();
        } else if (direction == SWIPE_DIRECTION_RIGHT) {
            create_quick_overlay();
        }
        return;
    }

    if (overlay_kind == OVERLAY_SETTINGS && direction == SWIPE_DIRECTION_UP) {
        close_overlay();
    } else if (overlay_kind == OVERLAY_QUICK && direction == SWIPE_DIRECTION_LEFT) {
        close_overlay();
    }
}

static K_WORK_DEFINE(overlay_work, overlay_work_cb);

static int swipe_gesture_listener(const zmk_event_t *eh) {
    const struct zmk_swipe_gesture_event *ev = as_zmk_swipe_gesture_event(eh);

    if (ev == NULL || !zmk_display_is_initialized()) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    pending_swipe = ev->direction;
    k_work_submit_to_queue(zmk_display_work_q(), &overlay_work);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(prospector_touch_ui, swipe_gesture_listener);
ZMK_SUBSCRIPTION(prospector_touch_ui, zmk_swipe_gesture_event);
