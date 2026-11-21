#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <lvgl.h>

// =============================================================================
// Capacitive Touch Driver — CST816T / CST816D (I2C 0x15)
// Feeds LVGL pointer input AND classifies gestures + continuous petting strokes.
// =============================================================================

enum class TouchEventType : uint8_t {
    NONE        = 0,
    TAP         = 1,
    DOUBLE_TAP  = 2,
    LONG_PRESS  = 3,
    SWIPE_LEFT  = 4,
    SWIPE_RIGHT = 5,
    SWIPE_UP    = 6,
    SWIPE_DOWN  = 7,
    PET_STROKE  = 8,
};

struct TouchEvent {
    TouchEventType type;
    int16_t        x;
    int16_t        y;
};

void       touch_cst816_init();
void       touch_cst816_register_lvgl();
void       touch_cst816_update();
bool       touch_cst816_is_pressed();
void       touch_cst816_get_xy(int16_t* x, int16_t* y);
TouchEvent touch_cst816_get_event();
uint32_t   touch_cst816_last_activity_ms();
void       touch_cst816_mark_activity();
