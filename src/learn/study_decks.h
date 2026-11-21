#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// ZooGotchi S3 — Spaced-Repetition Study Decks (Flash .rodata + PSRAM Custom)
// All strings are strictly printable ASCII so LVGL Montserrat renders crisply.
// =============================================================================

enum class StudyDeckId : uint8_t {
    DSA_PATTERNS = 0, // LeetCode / NeetCode 150 patterns (24 cards)
    DSA_BIGO     = 1, // Big-O, Python tips & interview edge cases (16 cards)
    LANG_ES      = 2, // Spanish vocabulary (20 cards)
    LANG_JP      = 3, // Japanese JLPT N4-N1 grammar, keigo & vocab (28 cards)
    ZOO_BIO      = 4, // Real-Zoo biology & Karimono genetics (18 cards)
    CUSTOM       = 5, // User-uploaded deck from Phone Web UI (<= 40 cards)
    COUNT        = 6,
};

static constexpr uint8_t STUDY_CUSTOM_MAX_CARDS = 40;
static constexpr uint8_t STUDY_MAX_CARDS_DECK   = 40;

struct StudyCard {
    const char* prompt;
    const char* choices[4];
    uint8_t     correctIdx;
    const char* takeaway;
    const char* termTag;
};

const char* study_deck_name(StudyDeckId deck);
uint8_t     study_deck_card_count(StudyDeckId deck);
bool        study_deck_get_card(StudyDeckId deck, uint8_t idx, StudyCard* out_card);

bool        study_custom_deck_reload();
bool        study_custom_deck_save_json(const char* json_str, size_t len);
