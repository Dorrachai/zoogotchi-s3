#include "study_decks.h"
#include "decks/dsa_patterns.h"
#include "decks/dsa_bigo.h"
#include "decks/spanish.h"
#include "decks/japanese_jlpt.h"
#include "decks/zoo_bio.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <string.h>

// =============================================================================
// Custom Phone-Uploaded Deck (PSRAM-backed, /custom_deck.json)
// =============================================================================
struct CustomCardBuf {
    char    prompt[192];
    char    choices[4][64];
    uint8_t correctIdx;
    char    takeaway[160];
    char    termTag[48];
};

static void utf8_strlcpy(char* dst, const char* src, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlcpy(dst, src, dst_size);
    if (n >= dst_size) {
        size_t end = dst_size - 1;
        while (end > 0 && ((uint8_t)dst[end - 1] & 0xC0) == 0x80) end--;
        if (end > 0 && ((uint8_t)dst[end - 1] & 0x80) != 0) end--;
        dst[end] = '\0';
    }
}

static CustomCardBuf* _custom_cards = nullptr;
static uint8_t        _custom_count = 0;
static const char*    CUSTOM_PATH   = "/custom_deck.json";
static const char*    CUSTOM_TMP    = "/custom_deck.tmp";

const char* study_deck_name(StudyDeckId deck) {
    switch (deck) {
    case StudyDeckId::DSA_PATTERNS: return "LeetCode Patterns";
    case StudyDeckId::DSA_BIGO:     return "Big-O & Edge";
    case StudyDeckId::LANG_ES:      return "Spanish Vocab";
    case StudyDeckId::LANG_JP:      return "Japanese JLPT";
    case StudyDeckId::ZOO_BIO:      return "Zoo Biology";
    case StudyDeckId::CUSTOM:       return "Custom Deck";
    default:                        return "Study";
    }
}

uint8_t study_deck_card_count(StudyDeckId deck) {
    switch (deck) {
    case StudyDeckId::DSA_PATTERNS: return sizeof(kDeckDsaPatterns) / sizeof(kDeckDsaPatterns[0]);
    case StudyDeckId::DSA_BIGO:     return sizeof(kDeckDsaBigO)     / sizeof(kDeckDsaBigO[0]);
    case StudyDeckId::LANG_ES:      return sizeof(kDeckLangEs)      / sizeof(kDeckLangEs[0]);
    case StudyDeckId::LANG_JP:      return sizeof(kDeckLangJp)      / sizeof(kDeckLangJp[0]);
    case StudyDeckId::ZOO_BIO:      return sizeof(kDeckZooBio)      / sizeof(kDeckZooBio[0]);
    case StudyDeckId::CUSTOM:       return _custom_count;
    default:                        return 0;
    }
}

bool study_deck_get_card(StudyDeckId deck, uint8_t idx, StudyCard* out_card) {
    if (!out_card) return false;
    uint8_t cnt = study_deck_card_count(deck);
    if (idx >= cnt) return false;

    switch (deck) {
    case StudyDeckId::DSA_PATTERNS: *out_card = kDeckDsaPatterns[idx]; return true;
    case StudyDeckId::DSA_BIGO:     *out_card = kDeckDsaBigO[idx];     return true;
    case StudyDeckId::LANG_ES:      *out_card = kDeckLangEs[idx];      return true;
    case StudyDeckId::LANG_JP:      *out_card = kDeckLangJp[idx];      return true;
    case StudyDeckId::ZOO_BIO:      *out_card = kDeckZooBio[idx];      return true;
    case StudyDeckId::CUSTOM:
        if (!_custom_cards) return false;
        out_card->prompt     = _custom_cards[idx].prompt;
        for (int i = 0; i < 4; i++) out_card->choices[i] = _custom_cards[idx].choices[i];
        out_card->correctIdx = _custom_cards[idx].correctIdx;
        out_card->takeaway   = _custom_cards[idx].takeaway;
        out_card->termTag    = _custom_cards[idx].termTag;
        return true;
    default:
        return false;
    }
}

static void ensure_custom_buf() {
    if (_custom_cards) return;
    size_t sz = sizeof(CustomCardBuf) * STUDY_CUSTOM_MAX_CARDS;
    _custom_cards = (CustomCardBuf*)heap_caps_calloc(1, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_custom_cards) _custom_cards = (CustomCardBuf*)calloc(1, sz);
}

bool study_custom_deck_reload() {
    ensure_custom_buf();
    _custom_count = 0;
    if (!_custom_cards || !LittleFS.exists(CUSTOM_PATH)) return false;

    File f = LittleFS.open(CUSTOM_PATH, "r");
    if (!f) return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err || !doc["cards"].is<JsonArray>()) return false;

    for (JsonObject o : doc["cards"].as<JsonArray>()) {
        if (_custom_count >= STUDY_CUSTOM_MAX_CARDS) break;
        const char* q = o["q"] | "";
        if (!q[0] || !o["c"].is<JsonArray>()) continue;
        JsonArray cs = o["c"].as<JsonArray>();
        if (cs.size() < 4) continue;

        CustomCardBuf& dst = _custom_cards[_custom_count];
        memset(&dst, 0, sizeof(dst));
        utf8_strlcpy(dst.prompt, q, sizeof(dst.prompt));
        for (int i = 0; i < 4; i++) {
            utf8_strlcpy(dst.choices[i], cs[i] | "-", sizeof(dst.choices[i]));
        }
        uint8_t a = (uint8_t)(o["a"] | 0);
        dst.correctIdx = a < 4 ? a : 0;
        utf8_strlcpy(dst.takeaway, o["e"] | dst.choices[dst.correctIdx], sizeof(dst.takeaway));
        utf8_strlcpy(dst.termTag,  o["t"] | dst.choices[dst.correctIdx], sizeof(dst.termTag));
        _custom_count++;
    }
    Serial.printf("[study] Loaded %u custom cards\n", (unsigned)_custom_count);
    return true;
}

bool study_custom_deck_save_json(const char* json_str, size_t len) {
    if (!json_str || len == 0 || len > 24576) return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json_str, len);
    if (err || !doc["cards"].is<JsonArray>()) return false;

    File f = LittleFS.open(CUSTOM_TMP, "w");
    if (!f) return false;
    size_t written = f.write((const uint8_t*)json_str, len);
    f.close();
    if (written != len) return false;
    if (LittleFS.exists(CUSTOM_PATH)) LittleFS.remove(CUSTOM_PATH);
    if (!LittleFS.rename(CUSTOM_TMP, CUSTOM_PATH)) return false;

    return study_custom_deck_reload();
}
