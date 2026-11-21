#pragma once
#include "minigame_common.h"

void           habitat_care_start(lv_obj_t* parent, uint32_t bg_hex, uint32_t acc_hex, const char* care_label);
MiniGameResult habitat_care_update(uint32_t now_ms);
void           habitat_care_stop();
