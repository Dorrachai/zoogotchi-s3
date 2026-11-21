#pragma once
#include <lvgl.h>
#include "lgfx_config.h"

void          lvgl_port_init(bool bgr_order = false);
void          lvgl_port_tick();
void          display_alert_flash(uint8_t flashes);
void          lvgl_port_set_brightness(uint8_t b);
void          lvgl_port_set_panel_sleep(bool sleep);
lv_display_t* lvgl_get_disp();
