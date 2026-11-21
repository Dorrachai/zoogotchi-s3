#include "focus_timer.h"

static bool     _active       = false;
static uint16_t _duration_min = 25;
static uint32_t _start_ms     = 0;
static uint32_t _total_ms     = 0;

void focus_timer_start(uint16_t minutes, uint32_t now_ms) {
    if (minutes < 1) minutes = 1;
    _duration_min = minutes;
    _start_ms     = now_ms;
    _total_ms     = (uint32_t)minutes * 60000UL;
    _active       = true;
}

void focus_timer_stop() {
    _active = false;
}

bool focus_timer_is_active() { return _active; }
uint16_t focus_timer_duration_min() { return _duration_min; }

uint32_t focus_timer_remaining_sec(uint32_t now_ms) {
    if (!_active) return 0;
    uint32_t elapsed = now_ms - _start_ms;
    if (elapsed >= _total_ms) return 0;
    return (_total_ms - elapsed + 999UL) / 1000UL;
}

uint8_t focus_timer_progress_pct(uint32_t now_ms) {
    if (!_active || _total_ms == 0) return 0;
    uint32_t elapsed = now_ms - _start_ms;
    if (elapsed >= _total_ms) return 100;
    return (uint8_t)((uint64_t)elapsed * 100ULL / _total_ms);
}

bool focus_timer_check_complete(uint32_t now_ms) {
    if (!_active) return false;
    if ((now_ms - _start_ms) >= _total_ms) {
        _active = false;
        return true;
    }
    return false;
}
