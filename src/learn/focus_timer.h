#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// Focus / Pomodoro Study Buddy Timer (15m / 25m LeetCode Sprint / 35m Mock)
// =============================================================================

void     focus_timer_start(uint16_t minutes, uint32_t now_ms);
void     focus_timer_stop();
bool     focus_timer_is_active();
uint16_t focus_timer_duration_min();
uint32_t focus_timer_remaining_sec(uint32_t now_ms);
uint8_t  focus_timer_progress_pct(uint32_t now_ms);
bool     focus_timer_check_complete(uint32_t now_ms);
