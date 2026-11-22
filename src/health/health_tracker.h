#pragma once
#include "../zoo/zoo_engine.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// Health & Wellness Assistant (/health.json, < 1 KB SRAM)
// Tracks daily step goals with RTC midnight rollover, 7-day history & streak,
// hydration cups synced with pet care, daytime nudges, and 4-4-4-4 breathing.
// =============================================================================

struct HealthState {
    uint32_t stepGoal;
    uint8_t  waterCupsToday;
    uint8_t  waterGoalCups;
    uint16_t goalStreakDays;
    uint16_t breatheSessionsTotal;
    uint32_t lastDayNum;
    uint32_t weekSteps[7]; // [0] = 7 days ago ... [6] = yesterday
    bool     goalCelebratedToday;
    bool     remindersEnabled;
    uint16_t reminderIntervalMin;
    int64_t  lastMoveEpoch;
    int64_t  lastWaterEpoch;
    int64_t  lastNudgeEpoch;
};

struct BreatheSnapshot {
    bool        active;
    const char* phase;
    uint8_t     phaseSecLeft;
    uint8_t     cycleNum;
    uint8_t     totalCycles;
    uint8_t     pct;
};

void               health_init(int64_t now_epoch, int16_t tz_offset_min);
bool               health_save();
const HealthState& health_get_state();

bool     health_check_midnight_rollover(ZooPetState* pet, int64_t now_epoch, int16_t tz_offset_min);
bool     health_on_steps(ZooPetState* pet, uint32_t new_steps, int64_t now_epoch);
void     health_log_water(ZooPetState* pet, int64_t now_epoch);
void     health_adjust_step_goal(int32_t delta);
void     health_set_config(uint32_t step_goal, uint8_t water_goal, bool reminders_on, uint16_t reminder_min);
void     health_toggle_reminders();
uint32_t health_week_avg_steps(uint32_t steps_today);
bool     health_check_nudge(int64_t now_epoch, uint8_t local_hour, bool busy_or_sleeping,
                            char* out_msg, size_t out_len);

void            health_breathe_start(uint32_t now_ms);
void            health_breathe_stop();
bool            health_breathe_is_active();
BreatheSnapshot health_breathe_snapshot(uint32_t now_ms);
bool            health_breathe_check_complete(uint32_t now_ms);
