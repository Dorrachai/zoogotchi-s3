#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// Power & Sleep Controller — 4-Stage 1000mAh LiPo Saver + Deep Sleep
// =============================================================================

enum class PowerMode : uint8_t {
    ACTIVE      = 0,
    DIM         = 1,
    LIGHT_SLEEP = 2,
    DEEP_SLEEP  = 3,
};

void      sleep_controller_init(uint8_t normal_brightness);
void      sleep_controller_set_normal_brightness(uint8_t b);
void      sleep_controller_mark_activity();
void      sleep_controller_update(bool wifi_companion_active, bool llm_busy, bool minigame_active, void (*pre_sleep_save_cb)(void));
void      sleep_controller_toggle_screen_sleep();
void      sleep_controller_power_off(void (*pre_sleep_save_cb)(void));
void      sleep_boost_cpu(bool high_perf);
PowerMode sleep_controller_mode();
