#pragma once
#include "minigame_common.h"

void           rhythm_chirp_start(lv_obj_t* parent, uint32_t bg_hex);
MiniGameResult rhythm_chirp_update(uint32_t now_ms);
void           rhythm_chirp_stop();
