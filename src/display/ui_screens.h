#pragma once
#include "../zoo/zoo_engine.h"
#include "../config_mgr/config_manager.h"
#include "../learn/study_decks.h"
#include "../health/health_tracker.h"
#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// ZooGotchi S3 — Rounded-Corner Touch UI State Machine (240x280 LVGL v9)
// =============================================================================

enum class UiScreen : uint8_t {
    ADOPT_CAT     = 0,
    ADOPT_SPECIES = 1,
    HOME          = 2,
    CARE          = 3,
    PLAY          = 4,
    SHOP          = 5,
    JOURNAL       = 6,
    MEMORIAL      = 7,
    SETTINGS      = 8,
    MINIGAME      = 9,
    STUDY_HUB     = 10,
    QUIZ          = 11,
    FOCUS         = 12,
    HEALTH        = 13,
};

struct UiCallbacks {
    void (*onAdoptSpecies)(uint8_t species_id);
    void (*onZooAction)(ZooAction act);
    void (*onAskPetTalk)();
    void (*onBuyUseShop)(uint8_t item_idx);
    void (*onBreedPlaza)();
    void (*onStartMiniGame)(uint8_t game_idx); // 0=Forage, 1=HabitatTap, 2=Rhythm
    void (*onToggleWifi)();
    void (*onToggleMute)();
    void (*onToggleDemo)();
    void (*onToggleBgr)();
    void (*onAdjustClockHour)(int8_t delta_hr);
    void (*onOpenStudyHub)();
    void (*onStartQuizDeck)(uint8_t deck_id);
    void (*onQuizAnswer)(uint8_t choice_idx);
    void (*onQuizNext)();
    void (*onOpenFocus)();
    void (*onStartFocus)(uint16_t minutes);
    void (*onStopFocus)();
    void (*onOpenHealth)();
    void (*onLogWater)();
    void (*onAdjustStepGoal)(int32_t delta);
    void (*onToggleHealthReminders)();
    void (*onToggleBreathe)();
};

void     ui_screens_init(const UiCallbacks& cbs);
void     ui_show_screen(UiScreen scr);
UiScreen ui_current_screen();
void     ui_update_home(const ZooPetState* pet, const AppConfig* cfg,
                        uint8_t local_hour, uint8_t local_min,
                        uint8_t battery_pct, bool wifi_on, uint32_t now_ms);
void     ui_show_speech_bubble(const char* text, uint32_t duration_ms = 6500);
void     ui_hide_speech_bubble();
void     ui_refresh_settings(const AppConfig* cfg, bool wifi_on, const char* ssid, const char* url);
void     ui_refresh_memorial(const ZooPetState* dead_pet);

void     ui_refresh_study_hub(int64_t now_epoch);
void     ui_show_quiz_card(const char* deck_name, uint8_t q_num, uint8_t q_total, const StudyCard* card);
void     ui_show_quiz_feedback(uint8_t picked_idx, uint8_t correct_idx, const char* takeaway);
void     ui_refresh_focus(bool active, uint16_t duration_min, uint32_t rem_sec, uint8_t pct,
                          bool demo_speed, const char* pet_name);
void     ui_refresh_health(const ZooPetState* pet, const HealthState* hs, const BreatheSnapshot* br);

lv_obj_t* ui_minigame_parent();
