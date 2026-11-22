#include "health_tracker.h"
#include "../rtc/rtc_driver.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>

static const char* HEALTH_PATH = "/health.json";
static const char* HEALTH_TMP  = "/health.tmp";

static constexpr uint32_t STEP_GOAL_MIN        = 2000;
static constexpr uint32_t STEP_GOAL_MAX        = 20000;
static constexpr uint8_t  WATER_GOAL_MIN       = 4;
static constexpr uint8_t  WATER_GOAL_MAX       = 16;
static constexpr uint16_t REMINDER_MIN_LO      = 30;
static constexpr uint16_t REMINDER_MIN_HI      = 180;
static constexpr uint32_t VALID_MIN_DAY_NUM    = 18000; // >= ~2019-04-14
static constexpr uint32_t BREATHE_DURATION_MS  = 64000UL; // 4 cycles x (4+4+4+4)s

static HealthState _hs          = {};
static bool        _br_active   = false;
static uint32_t    _br_start_ms = 0;

static uint32_t clamp_step_goal(uint32_t g) {
    if (g < STEP_GOAL_MIN) return STEP_GOAL_MIN;
    if (g > STEP_GOAL_MAX) return STEP_GOAL_MAX;
    return g;
}

static uint8_t clamp_water_goal(uint8_t g) {
    if (g < WATER_GOAL_MIN) return WATER_GOAL_MIN;
    if (g > WATER_GOAL_MAX) return WATER_GOAL_MAX;
    return g;
}

static uint16_t clamp_reminder_min(uint16_t m) {
    if (m < REMINDER_MIN_LO) return REMINDER_MIN_LO;
    if (m > REMINDER_MIN_HI) return REMINDER_MIN_HI;
    return m;
}

static uint8_t clamp_u8_add(uint8_t v, int d) {
    int r = (int)v + d;
    if (r < 0) return 0;
    if (r > 100) return 100;
    return (uint8_t)r;
}

static void apply_defaults(int64_t now_epoch, int16_t tz_offset_min) {
    memset(&_hs, 0, sizeof(_hs));
    _hs.stepGoal            = 6000;
    _hs.waterGoalCups       = 8;
    _hs.remindersEnabled    = true;
    _hs.reminderIntervalMin = 60;
    uint32_t day = rtc_epoch_to_local_day((time_t)now_epoch, tz_offset_min);
    _hs.lastDayNum          = (day >= VALID_MIN_DAY_NUM) ? day : 0;
    _hs.lastMoveEpoch       = now_epoch;
    _hs.lastWaterEpoch      = now_epoch;
    _hs.lastNudgeEpoch      = now_epoch;
}

void health_init(int64_t now_epoch, int16_t tz_offset_min) {
    apply_defaults(now_epoch, tz_offset_min);
    _br_active   = false;
    _br_start_ms = 0;

    if (!LittleFS.exists(HEALTH_PATH)) return;
    File f = LittleFS.open(HEALTH_PATH, "r");
    if (!f) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;

    _hs.stepGoal            = clamp_step_goal(doc["stepGoal"] | 6000UL);
    _hs.waterCupsToday      = (uint8_t)(doc["water"] | 0);
    _hs.waterGoalCups       = clamp_water_goal((uint8_t)(doc["waterGoal"] | 8));
    _hs.goalStreakDays      = (uint16_t)(doc["streak"] | 0);
    _hs.breatheSessionsTotal= (uint16_t)(doc["breathe"] | 0);
    _hs.lastDayNum          = doc["day"] | _hs.lastDayNum;
    _hs.goalCelebratedToday = doc["doneToday"] | false;
    _hs.remindersEnabled    = doc["nudge"] | true;
    _hs.reminderIntervalMin = clamp_reminder_min((uint16_t)(doc["nudgeMin"] | 60));
    _hs.lastMoveEpoch       = doc["lastMove"] | now_epoch;
    _hs.lastWaterEpoch      = doc["lastWater"] | now_epoch;
    _hs.lastNudgeEpoch      = doc["lastNudge"] | now_epoch;

    if (doc["week"].is<JsonArray>()) {
        JsonArray arr = doc["week"].as<JsonArray>();
        uint8_t idx = 0;
        for (JsonVariant v : arr) {
            if (idx >= 7) break;
            _hs.weekSteps[idx++] = v | 0UL;
        }
    }
}

bool health_save() {
    JsonDocument doc;
    doc["stepGoal"]  = _hs.stepGoal;
    doc["water"]     = _hs.waterCupsToday;
    doc["waterGoal"] = _hs.waterGoalCups;
    doc["streak"]    = _hs.goalStreakDays;
    doc["breathe"]   = _hs.breatheSessionsTotal;
    doc["day"]       = _hs.lastDayNum;
    doc["doneToday"] = _hs.goalCelebratedToday;
    doc["nudge"]     = _hs.remindersEnabled;
    doc["nudgeMin"]  = _hs.reminderIntervalMin;
    doc["lastMove"]  = _hs.lastMoveEpoch;
    doc["lastWater"] = _hs.lastWaterEpoch;
    doc["lastNudge"] = _hs.lastNudgeEpoch;

    JsonArray arr = doc["week"].to<JsonArray>();
    for (int i = 0; i < 7; i++) arr.add(_hs.weekSteps[i]);

    File f = LittleFS.open(HEALTH_TMP, "w");
    if (!f) return false;
    size_t n = serializeJson(doc, f);
    f.close();
    if (n == 0) return false;
    if (LittleFS.exists(HEALTH_PATH)) LittleFS.remove(HEALTH_PATH);
    return LittleFS.rename(HEALTH_TMP, HEALTH_PATH);
}

const HealthState& health_get_state() { return _hs; }

bool health_check_midnight_rollover(ZooPetState* pet, int64_t now_epoch, int16_t tz_offset_min) {
    uint32_t cur_day = rtc_epoch_to_local_day((time_t)now_epoch, tz_offset_min);
    if (cur_day < VALID_MIN_DAY_NUM) return false;

    if (_hs.lastDayNum == 0) {
        _hs.lastDayNum = cur_day;
        health_save();
        return false;
    }
    if (cur_day == _hs.lastDayNum) return false;

    if (cur_day < _hs.lastDayNum) {
        _hs.lastDayNum = cur_day;
        health_save();
        return false;
    }

    uint32_t delta_days = cur_day - _hs.lastDayNum;
    uint32_t prev_steps = pet ? pet->stepsToday : 0;

    // Shift rolling 7-day history ([6] = yesterday)
    for (int i = 0; i < 6; i++) _hs.weekSteps[i] = _hs.weekSteps[i + 1];
    _hs.weekSteps[6] = prev_steps;
    uint32_t rem = delta_days - 1;
    if (rem > 7) rem = 7;
    for (uint32_t s = 0; s < rem; s++) {
        for (int i = 0; i < 6; i++) _hs.weekSteps[i] = _hs.weekSteps[i + 1];
        _hs.weekSteps[6] = 0;
    }

    if (delta_days > 1) {
        _hs.goalStreakDays = 0;
    } else if (!_hs.goalCelebratedToday) {
        if (prev_steps >= _hs.stepGoal) _hs.goalStreakDays++;
        else                            _hs.goalStreakDays = 0;
    }

    if (pet) pet->stepsToday = 0;
    _hs.waterCupsToday      = 0;
    _hs.goalCelebratedToday = false;
    _hs.lastDayNum          = cur_day;
    health_save();
    return true;
}

bool health_on_steps(ZooPetState* pet, uint32_t new_steps, int64_t now_epoch) {
    if (new_steps > 0) {
        _hs.lastMoveEpoch = now_epoch;
    }
    if (pet && pet->hasActivePet && !pet->isDead &&
        !_hs.goalCelebratedToday && pet->stepsToday >= _hs.stepGoal) {
        _hs.goalCelebratedToday = true;
        _hs.goalStreakDays++;
        health_save();
        return true;
    }
    return false;
}

void health_log_water(ZooPetState* pet, int64_t now_epoch) {
    if (_hs.waterCupsToday < 30) _hs.waterCupsToday++;
    _hs.lastWaterEpoch = now_epoch;
    _hs.lastNudgeEpoch = now_epoch;
    if (pet && pet->hasActivePet && !pet->isDead) {
        pet->hydration = clamp_u8_add(pet->hydration, 10);
        pet->bond      = clamp_u8_add(pet->bond, 2);
        pet->zooCoins += 2;
    }
    health_save();
}

void health_adjust_step_goal(int32_t delta) {
    int64_t next = (int64_t)_hs.stepGoal + delta;
    if (next < (int64_t)STEP_GOAL_MIN) next = STEP_GOAL_MIN;
    if (next > (int64_t)STEP_GOAL_MAX) next = STEP_GOAL_MAX;
    _hs.stepGoal = (uint32_t)next;
    health_save();
}

void health_set_config(uint32_t step_goal, uint8_t water_goal, bool reminders_on, uint16_t reminder_min) {
    _hs.stepGoal            = clamp_step_goal(step_goal);
    _hs.waterGoalCups       = clamp_water_goal(water_goal);
    _hs.remindersEnabled    = reminders_on;
    _hs.reminderIntervalMin = clamp_reminder_min(reminder_min);
    health_save();
}

void health_toggle_reminders() {
    _hs.remindersEnabled = !_hs.remindersEnabled;
    health_save();
}

uint32_t health_week_avg_steps(uint32_t steps_today) {
    uint64_t sum = steps_today;
    uint32_t cnt = 1;
    for (int i = 0; i < 7; i++) {
        if (_hs.weekSteps[i] > 0) {
            sum += _hs.weekSteps[i];
            cnt++;
        }
    }
    return (uint32_t)(sum / cnt);
}

bool health_check_nudge(int64_t now_epoch, uint8_t local_hour, bool busy_or_sleeping,
                        char* out_msg, size_t out_len) {
    if (!_hs.remindersEnabled || busy_or_sleeping || !out_msg || out_len < 16) return false;
    if (local_hour < 8 || local_hour >= 21) return false;
    int64_t interval_s = (int64_t)_hs.reminderIntervalMin * 60LL;
    if ((now_epoch - _hs.lastNudgeEpoch) < interval_s) return false;

    if (_hs.lastMoveEpoch > 0 && (now_epoch - _hs.lastMoveEpoch) >= interval_s) {
        _hs.lastNudgeEpoch = now_epoch;
        snprintf(out_msg, out_len, "Time for a quick stretch & walk!");
        return true;
    }
    if (_hs.waterCupsToday < _hs.waterGoalCups &&
        _hs.lastWaterEpoch > 0 && (now_epoch - _hs.lastWaterEpoch) >= interval_s * 2LL) {
        _hs.lastNudgeEpoch = now_epoch;
        snprintf(out_msg, out_len, "Hydration check: %u/%u cups today!",
                 (unsigned)_hs.waterCupsToday, (unsigned)_hs.waterGoalCups);
        return true;
    }
    return false;
}

void health_breathe_start(uint32_t now_ms) {
    _br_active   = true;
    _br_start_ms = now_ms;
}

void health_breathe_stop() {
    _br_active = false;
}

bool health_breathe_is_active() { return _br_active; }

BreatheSnapshot health_breathe_snapshot(uint32_t now_ms) {
    BreatheSnapshot snap = {false, "Ready", 4, 1, 4, 0};
    if (!_br_active) return snap;

    uint32_t elapsed = now_ms - _br_start_ms;
    if (elapsed >= BREATHE_DURATION_MS) elapsed = BREATHE_DURATION_MS;
    uint32_t es = elapsed / 1000UL;
    uint8_t  ph = (uint8_t)((es / 4UL) % 4UL);
    static const char* PHASES[4] = {"Inhale", "Hold", "Exhale", "Hold"};

    snap.active       = true;
    snap.phase        = PHASES[ph];
    snap.phaseSecLeft = (uint8_t)(4UL - (es % 4UL));
    snap.cycleNum     = (uint8_t)((es / 16UL) + 1UL);
    if (snap.cycleNum > 4) snap.cycleNum = 4;
    snap.totalCycles  = 4;
    snap.pct          = (uint8_t)((elapsed * 100UL) / BREATHE_DURATION_MS);
    return snap;
}

bool health_breathe_check_complete(uint32_t now_ms) {
    if (!_br_active) return false;
    if ((now_ms - _br_start_ms) < BREATHE_DURATION_MS) return false;
    _br_active = false;
    _hs.breatheSessionsTotal++;
    health_save();
    return true;
}
