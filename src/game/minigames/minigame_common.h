#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <lvgl.h>

struct MiniGameResult {
    bool     finished;
    uint16_t score;
    uint16_t coinsEarned;
    int8_t   braveryDelta;
    char     summary[48];
};
