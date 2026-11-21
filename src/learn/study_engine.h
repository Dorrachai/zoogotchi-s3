#pragma once
#include "study_decks.h"
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// ZooGotchi S3 — SM-2 / Leitner Spaced Repetition & Study Progress Engine
// Uses PCF85063A RTC epoch so card review intervals stay accurate off-power.
// =============================================================================

struct StudyCardProgress {
    uint8_t box;       // 0 = unseen/missed, 1..4 = learning, 5 = mastered
    int64_t dueEpoch;  // 0 = unseen/immediately due
};

struct StudyStats {
    uint32_t xpTotal;
    uint16_t studyStreakDays;
    uint32_t lastStudyDayNum;
    uint32_t focusMinutesTotal;
    uint32_t totalAnswered;
    uint32_t totalCorrect;
};

void              study_init();
bool              study_save();
const StudyStats& study_get_stats();

uint8_t  study_due_count(StudyDeckId deck, int64_t now_epoch);
uint16_t study_total_due_count(int64_t now_epoch);
uint16_t study_mastered_count();
bool     study_pick_next_card(StudyDeckId deck, int64_t now_epoch, uint8_t* out_card_idx);

// Updates box & due time, increments XP/streak, returns Zoo Coins earned.
uint16_t study_record_answer(StudyDeckId deck, uint8_t card_idx, bool correct, int64_t now_epoch);
uint16_t study_record_focus_session(uint16_t minutes, int64_t now_epoch);
