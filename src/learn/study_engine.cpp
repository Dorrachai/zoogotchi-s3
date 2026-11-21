#include "study_engine.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <string.h>

static const char* STUDY_PATH = "/study_progress.json";
static const char* STUDY_TMP  = "/study_progress.tmp";

// Leitner / SM-2 review intervals in hours for boxes 0..5
static constexpr uint16_t BOX_INTERVAL_HR[6] = {0, 4, 24, 72, 168, 360};

static StudyStats        _stats = {};
static StudyCardProgress _prog[(uint8_t)StudyDeckId::COUNT][STUDY_MAX_CARDS_DECK] = {};

static void update_streak(int64_t now_epoch) {
    if (now_epoch <= 0) return;
    uint32_t day_num = (uint32_t)(now_epoch / 86400LL);
    if (_stats.lastStudyDayNum == 0) {
        _stats.studyStreakDays = 1;
    } else if (day_num == _stats.lastStudyDayNum + 1) {
        _stats.studyStreakDays++;
    } else if (day_num > _stats.lastStudyDayNum + 1) {
        _stats.studyStreakDays = 1;
    }
    _stats.lastStudyDayNum = day_num;
}

void study_init() {
    memset(&_stats, 0, sizeof(_stats));
    memset(_prog, 0, sizeof(_prog));
    study_custom_deck_reload();

    if (!LittleFS.exists(STUDY_PATH)) return;
    File f = LittleFS.open(STUDY_PATH, "r");
    if (!f) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;

    _stats.xpTotal           = doc["xp"]     | 0UL;
    _stats.studyStreakDays   = doc["streak"] | 0;
    _stats.lastStudyDayNum   = doc["day"]    | 0UL;
    _stats.focusMinutesTotal = doc["focus"]  | 0UL;
    _stats.totalAnswered     = doc["ans"]    | 0UL;
    _stats.totalCorrect      = doc["ok"]     | 0UL;

    if (doc["cards"].is<JsonArray>()) {
        for (JsonArray row : doc["cards"].as<JsonArray>()) {
            if (row.size() < 4) continue;
            uint8_t d   = (uint8_t)(row[0] | 0);
            uint8_t c   = (uint8_t)(row[1] | 0);
            uint8_t box = (uint8_t)(row[2] | 0);
            int64_t due = (int64_t)(row[3] | 0LL);
            if (d < (uint8_t)StudyDeckId::COUNT && c < STUDY_MAX_CARDS_DECK) {
                _prog[d][c].box      = box > 5 ? 5 : box;
                _prog[d][c].dueEpoch = due;
            }
        }
    }
}

bool study_save() {
    JsonDocument doc;
    doc["xp"]     = _stats.xpTotal;
    doc["streak"] = _stats.studyStreakDays;
    doc["day"]    = _stats.lastStudyDayNum;
    doc["focus"]  = _stats.focusMinutesTotal;
    doc["ans"]    = _stats.totalAnswered;
    doc["ok"]     = _stats.totalCorrect;

    JsonArray arr = doc["cards"].to<JsonArray>();
    for (uint8_t d = 0; d < (uint8_t)StudyDeckId::COUNT; d++) {
        uint8_t cnt = study_deck_card_count((StudyDeckId)d);
        for (uint8_t c = 0; c < cnt && c < STUDY_MAX_CARDS_DECK; c++) {
            if (_prog[d][c].box == 0 && _prog[d][c].dueEpoch == 0) continue;
            JsonArray row = arr.add<JsonArray>();
            row.add(d);
            row.add(c);
            row.add(_prog[d][c].box);
            row.add(_prog[d][c].dueEpoch);
        }
    }

    File f = LittleFS.open(STUDY_TMP, "w");
    if (!f) return false;
    size_t n = serializeJson(doc, f);
    f.close();
    if (n == 0) return false;
    if (LittleFS.exists(STUDY_PATH)) LittleFS.remove(STUDY_PATH);
    return LittleFS.rename(STUDY_TMP, STUDY_PATH);
}

const StudyStats& study_get_stats() { return _stats; }

uint8_t study_due_count(StudyDeckId deck, int64_t now_epoch) {
    uint8_t d = (uint8_t)deck;
    if (d >= (uint8_t)StudyDeckId::COUNT) return 0;
    uint8_t cnt = study_deck_card_count(deck);
    uint8_t due = 0;
    for (uint8_t c = 0; c < cnt; c++) {
        if (_prog[d][c].dueEpoch <= now_epoch) due++;
    }
    return due;
}

uint16_t study_total_due_count(int64_t now_epoch) {
    uint16_t tot = 0;
    for (uint8_t d = 0; d < (uint8_t)StudyDeckId::COUNT; d++) {
        tot += study_due_count((StudyDeckId)d, now_epoch);
    }
    return tot;
}

uint16_t study_mastered_count() {
    uint16_t m = 0;
    for (uint8_t d = 0; d < (uint8_t)StudyDeckId::COUNT; d++) {
        uint8_t cnt = study_deck_card_count((StudyDeckId)d);
        for (uint8_t c = 0; c < cnt; c++) {
            if (_prog[d][c].box >= 4) m++;
        }
    }
    return m;
}

bool study_pick_next_card(StudyDeckId deck, int64_t now_epoch, uint8_t* out_card_idx) {
    uint8_t d = (uint8_t)deck;
    uint8_t cnt = study_deck_card_count(deck);
    if (!out_card_idx || d >= (uint8_t)StudyDeckId::COUNT || cnt == 0) return false;

    // 1. Collect all due seen/unseen cards; pick randomly among earliest/due to avoid monotony
    uint8_t due_pool[STUDY_MAX_CARDS_DECK];
    uint8_t due_n = 0;
    for (uint8_t c = 0; c < cnt; c++) {
        if (_prog[d][c].dueEpoch <= now_epoch) {
            due_pool[due_n++] = c;
        }
    }
    if (due_n > 0) {
        *out_card_idx = due_pool[esp_random() % due_n];
        return true;
    }

    // 2. If all cards are ahead of schedule, allow practicing the soonest upcoming card
    uint8_t best_c = 0;
    int64_t earliest = _prog[d][0].dueEpoch;
    for (uint8_t c = 1; c < cnt; c++) {
        if (_prog[d][c].dueEpoch < earliest) {
            earliest = _prog[d][c].dueEpoch;
            best_c = c;
        }
    }
    *out_card_idx = best_c;
    return true;
}

uint16_t study_record_answer(StudyDeckId deck, uint8_t card_idx, bool correct, int64_t now_epoch) {
    uint8_t d = (uint8_t)deck;
    uint8_t cnt = study_deck_card_count(deck);
    if (d >= (uint8_t)StudyDeckId::COUNT || card_idx >= cnt) return 0;

    _stats.totalAnswered++;
    update_streak(now_epoch);

    StudyCardProgress& cp = _prog[d][card_idx];
    uint16_t coins = 1;
    if (correct) {
        _stats.totalCorrect++;
        if (cp.box < 5) cp.box++;
        _stats.xpTotal += (uint32_t)(10 + cp.box * 5);
        coins = 6;
    } else {
        cp.box = 0;
        _stats.xpTotal += 2;
        coins = 1;
    }

    int64_t wait_sec = (int64_t)BOX_INTERVAL_HR[cp.box] * 3600LL;
    if (!correct) wait_sec = 300; // missed cards return in 5 minutes
    cp.dueEpoch = now_epoch + wait_sec;

    study_save();
    return coins;
}

uint16_t study_record_focus_session(uint16_t minutes, int64_t now_epoch) {
    _stats.focusMinutesTotal += minutes;
    _stats.xpTotal += (uint32_t)(minutes * 4);
    update_streak(now_epoch);
    study_save();
    return (uint16_t)(5 + minutes);
}
