#pragma once
#include "minigame_common.h"

void           forage_catch_start(lv_obj_t* parent, uint32_t bg_hex, uint32_t acc_hex, const char* food_name);
MiniGameResult forage_catch_update(float tilt_x, int16_t touch_x, bool touch_down, uint32_t now_ms);
void           forage_catch_stop();
