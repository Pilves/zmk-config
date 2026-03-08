/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

#define GRID_COLS 5
#define GRID_ROWS 3
#define GRID_CELLS (GRID_COLS * GRID_ROWS)

struct zmk_widget_right_status {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *batt_label;
    lv_obj_t *device_label;
    lv_obj_t *left_labels[GRID_CELLS];
    lv_obj_t *right_labels[GRID_CELLS];
    uint8_t current_layer;
    uint8_t battery;
    bool connected;
    int active_profile_index;
};

int zmk_widget_right_status_init(struct zmk_widget_right_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_right_status_obj(struct zmk_widget_right_status *widget);
