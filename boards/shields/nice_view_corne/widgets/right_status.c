/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Peripheral (right half) display for Corne Choc Pro.
 * Shows key grid for active layer + battery/device name top bar.
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/battery.h>
#include <zmk/keymap.h>

#if defined(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif

#include "right_status.h"

#define NUM_LAYERS 4

/* ---------- key label tables ---------- */

// Left hand inner 5 columns (excluding outermost pinky column)
static const char *left_keys[NUM_LAYERS][GRID_CELLS] = {
    // Layer 0 — Graphite
    {"B", "L", "D", "W", "Z",
     "N", "R", "T", "S", "G",
     "Q", "X", "M", "C", "V"},
    // Layer 1 — Navigate
    {"F2", "F3", "F4", "F5", "F6",
     "GUI", "ALT", "CTL", "SFT", "TAB",
     "1", "2", "3", "4", "5"},
    // Layer 2 — Symbols
    {"@", "`", "'", "\"", "+",
     "?", "{}", "[]", "()", "*",
     "!", "<", ">", "=>", "#"},
    // Layer 3 — Adjust
    {"BT0", "BT1", "BT2", "BT3", "BT4",
     "BRI+", "BRI-", "SN+", "SN-", "",
     "HUE+", "HUE-", "SAT+", "SAT-", ""},
};

// Right hand inner 5 columns (excluding outermost pinky column)
static const char *right_keys[NUM_LAYERS][GRID_CELLS] = {
    // Layer 0 — Graphite
    {"-", "F", "O", "U", "J",
     "Y", "H", "A", "E", "I",
     "K", "P", ",", ".", "/"},
    // Layer 1 — Navigate
    {"F7", "F8", "F9", "F10", "F11",
     "HOM", "<", "v", "^", ">",
     "6", "7", "8", "9", "0"},
    // Layer 2 — Symbols
    {"~", "$", "&", "%", "\\",
     "|", "=", "_", "^", "EUR",
     ":", ";", ",", ".", "/"},
    // Layer 3 — Adjust
    {"", "", "", "", "",
     "ae", "o~", "ue", "oe", "SLP",
     "PRV", "V-", "MUT", "V+", "NXT"},
};

/* ---------- BT slot device names ---------- */

static const char *bt_slot_names[5] = {
    "MacBook", "Tablet", "BT2", "BT3", "BT4"
};

/* ---------- layout constants ---------- */

#define SCREEN_W  160
#define SCREEN_H  68
#define TOP_BAR_H 12
#define SEP_Y1    12
#define LEFT_Y    13
#define SEP_Y2    40
#define RIGHT_Y   41
#define KEY_COL_W 32
#define KEY_ROW_H 9

/* ---------- widget list ---------- */

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/* ---------- helpers ---------- */

static void update_keys(struct zmk_widget_right_status *widget, uint8_t layer) {
    if (layer >= NUM_LAYERS) {
        layer = 0;
    }
    widget->current_layer = layer;
    for (int i = 0; i < GRID_CELLS; i++) {
        lv_label_set_text_static(widget->left_labels[i], left_keys[layer][i]);
        lv_label_set_text_static(widget->right_labels[i], right_keys[layer][i]);
    }
}

static void update_battery_label(struct zmk_widget_right_status *widget) {
    static char buf[5];
    snprintf(buf, sizeof(buf), "%d%%", widget->battery);
    lv_label_set_text(widget->batt_label, buf);
}

static void update_device_name(struct zmk_widget_right_status *widget) {
    if (!widget->connected) {
        lv_label_set_text_static(widget->device_label, "---");
        return;
    }
    int idx = widget->active_profile_index;
    if (idx < 0 || idx >= 5) {
        idx = 0;
    }
    lv_label_set_text_static(widget->device_label, bt_slot_names[idx]);
}

static void create_separator(lv_obj_t *parent, int y) {
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, SCREEN_W, 1);
    lv_obj_set_pos(line, 0, y);
    lv_obj_set_style_bg_color(line, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
}

static void create_key_label(lv_obj_t *parent, lv_obj_t **out, int col, int row, int grid_y) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl, col * KEY_COL_W, grid_y + row * KEY_ROW_H);
    lv_obj_set_size(lbl, KEY_COL_W, KEY_ROW_H);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_label_set_text_static(lbl, "");
    *out = lbl;
}

/* ---------- forward declarations for listener inits ---------- */

static void widget_layer_status_init(void);
static void widget_battery_status_init(void);
static void widget_peripheral_status_init(void);
#if defined(CONFIG_ZMK_BLE)
static void widget_output_status_init(void);
#endif

/* ---------- init ---------- */

int zmk_widget_right_status_init(struct zmk_widget_right_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(widget->obj);
    lv_obj_set_size(widget->obj, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(widget->obj, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);

    // --- top bar: battery (left) + device name (right) ---

    widget->batt_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->batt_label, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(widget->batt_label, lv_color_black(), 0);
    lv_obj_set_pos(widget->batt_label, 1, 2);
    lv_obj_set_size(widget->batt_label, 32, 10);
    lv_label_set_text(widget->batt_label, "0%");

    widget->device_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->device_label, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(widget->device_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(widget->device_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(widget->device_label, 40, 2);
    lv_obj_set_size(widget->device_label, 119, 10);
    lv_label_set_long_mode(widget->device_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text_static(widget->device_label, "---");

    // --- separators ---
    create_separator(widget->obj, SEP_Y1);
    create_separator(widget->obj, SEP_Y2);

    // --- left hand key grid ---
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            create_key_label(widget->obj, &widget->left_labels[r * GRID_COLS + c],
                             c, r, LEFT_Y);
        }
    }

    // --- right hand key grid ---
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            create_key_label(widget->obj, &widget->right_labels[r * GRID_COLS + c],
                             c, r, RIGHT_Y);
        }
    }

    // --- initial state ---
    widget->battery = zmk_battery_state_of_charge();
    widget->connected = zmk_split_bt_peripheral_is_connected();
    widget->active_profile_index = 0;

#if defined(CONFIG_ZMK_BLE)
    widget->active_profile_index = zmk_ble_active_profile_index();
#endif

    update_battery_label(widget);
    update_device_name(widget);
    update_keys(widget, 0);

    sys_slist_append(&widgets, &widget->node);

    widget_layer_status_init();
    widget_battery_status_init();
    widget_peripheral_status_init();
#if defined(CONFIG_ZMK_BLE)
    widget_output_status_init();
#endif

    return 0;
}

lv_obj_t *zmk_widget_right_status_obj(struct zmk_widget_right_status *widget) {
    return widget->obj;
}

/* ---------- layer state listener ---------- */

struct layer_status_state {
    uint8_t index;
};

static void set_layer_status(struct zmk_widget_right_status *widget,
                             struct layer_status_state state) {
    update_keys(widget, state.index);
}

static void layer_status_update_cb(struct layer_status_state state) {
    struct zmk_widget_right_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_layer_status(widget, state);
    }
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh) {
    return (struct layer_status_state){
        .index = zmk_keymap_highest_layer_active(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state,
                            layer_status_update_cb, layer_status_get_state)
ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

/* ---------- battery state listener ---------- */

struct battery_status_state {
    uint8_t level;
};

static void set_battery_status(struct zmk_widget_right_status *widget,
                               struct battery_status_state state) {
    widget->battery = state.level;
    update_battery_label(widget);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_right_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_battery_status(widget, state);
    }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);

/* ---------- connection state listener ---------- */

struct peripheral_status_state {
    bool connected;
};

static void set_connection_status(struct zmk_widget_right_status *widget,
                                  struct peripheral_status_state state) {
    widget->connected = state.connected;
    update_device_name(widget);
}

static void connection_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_right_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_connection_status(widget, state);
    }
}

static struct peripheral_status_state connection_status_get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){
        .connected = zmk_split_bt_peripheral_is_connected(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            connection_status_update_cb, connection_status_get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

/* ---------- BLE profile listener (device name) ---------- */

#if defined(CONFIG_ZMK_BLE)

struct output_status_state {
    int active_profile_index;
};

static void set_output_status(struct zmk_widget_right_status *widget,
                              const struct output_status_state *state) {
    widget->active_profile_index = state->active_profile_index;
    update_device_name(widget);
}

static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_right_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_output_status(widget, &state);
    }
}

static struct output_status_state output_status_get_state(const zmk_event_t *_eh) {
    return (struct output_status_state){
        .active_profile_index = zmk_ble_active_profile_index(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, output_status_get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);

#endif /* CONFIG_ZMK_BLE */
