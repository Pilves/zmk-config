/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_NICE_VIEW_CORNE_WIDGET)

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#include "widgets/central_status.h"
static struct zmk_widget_status status_widget;

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);
    zmk_widget_status_init(&status_widget, screen);
    lv_obj_align(zmk_widget_status_obj(&status_widget), LV_ALIGN_TOP_LEFT, 0, 0);
    return screen;
}

#else /* peripheral */

#include "widgets/right_status.h"
static struct zmk_widget_right_status right_widget;

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);
    zmk_widget_right_status_init(&right_widget, screen);
    lv_obj_align(zmk_widget_right_status_obj(&right_widget), LV_ALIGN_TOP_LEFT, 0, 0);
    return screen;
}

#endif /* CONFIG_ZMK_SPLIT_ROLE_CENTRAL */
#endif /* CONFIG_NICE_VIEW_CORNE_WIDGET */
