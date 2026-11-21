#include "personality_memory.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>

static const char* MEMORY_PATH = "/memory.json";
static const char* MEMORY_TMP  = "/memory.tmp";

void memory_init(PetMemory* m) {
    memset(m, 0, sizeof(PetMemory));
    strlcpy(m->favoriteFood, "fresh snacks", sizeof(m->favoriteFood));
    strlcpy(m->lastAction,   "woke up",      sizeof(m->lastAction));
}

bool memory_load(PetMemory* m) {
    memory_init(m);
    if (!LittleFS.exists(MEMORY_PATH)) return false;
    File f = LittleFS.open(MEMORY_PATH, "r");
    if (!f) return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    m->totalMeals = doc["meals"] | 0UL;
    m->totalPets  = doc["pets"]  | 0UL;
    m->totalGames = doc["games"] | 0UL;
    m->totalChats = doc["chats"] | 0UL;
    strlcpy(m->favoriteFood, doc["fav"]  | "fresh snacks", sizeof(m->favoriteFood));
    strlcpy(m->lastAction,   doc["last"] | "woke up",      sizeof(m->lastAction));
    strlcpy(m->lastLearned,  doc["lrn"]  | "",             sizeof(m->lastLearned));

    m->diaryCount = 0;
    if (doc["diary"].is<JsonArray>()) {
        for (JsonObject o : doc["diary"].as<JsonArray>()) {
            if (m->diaryCount >= DIARY_MAX_ENTRIES) break;
            DiaryEntry& e = m->diary[m->diaryCount++];
            e.tsEpoch = o["ts"] | 0LL;
            strlcpy(e.text, o["t"] | "", sizeof(e.text));
        }
    }
    return true;
}

bool memory_save(const PetMemory* m) {
    if (!m) return false;
    JsonDocument doc;
    doc["meals"] = m->totalMeals;
    doc["pets"]  = m->totalPets;
    doc["games"] = m->totalGames;
    doc["chats"] = m->totalChats;
    doc["fav"]   = m->favoriteFood;
    doc["last"]  = m->lastAction;
    doc["lrn"]   = m->lastLearned;

    JsonArray arr = doc["diary"].to<JsonArray>();
    for (uint8_t i = 0; i < m->diaryCount && i < DIARY_MAX_ENTRIES; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["ts"] = m->diary[i].tsEpoch;
        o["t"]  = m->diary[i].text;
    }

    File f = LittleFS.open(MEMORY_TMP, "w");
    if (!f) return false;
    size_t n = serializeJson(doc, f);
    f.close();
    if (n == 0) return false;
    if (LittleFS.exists(MEMORY_PATH)) LittleFS.remove(MEMORY_PATH);
    return LittleFS.rename(MEMORY_TMP, MEMORY_PATH);
}

void memory_record_event(PetMemory* m, const char* event_text, int64_t epoch) {
    if (!m || !event_text || !event_text[0]) return;
    if (m->diaryCount >= DIARY_MAX_ENTRIES) {
        memmove(&m->diary[0], &m->diary[1], sizeof(DiaryEntry) * (DIARY_MAX_ENTRIES - 1));
        m->diaryCount = DIARY_MAX_ENTRIES - 1;
    }
    DiaryEntry& e = m->diary[m->diaryCount++];
    e.tsEpoch = epoch;
    strlcpy(e.text, event_text, sizeof(e.text));
    memory_save(m);
}

void memory_build_prompt_context(const ZooPetState* s, const PetMemory* m,
                                 uint8_t local_hour, char* out, size_t out_len) {
    if (!out || out_len == 0) return;
    if (!s || !s->hasActivePet) {
        strlcpy(out, "[SP:ZOO] [ST:EGG] [MD:CALM]", out_len);
        return;
    }
    const SpeciesProfile& sp = species_get(s->speciesId);
    PetMood mood = zoo_compute_mood(s, 0);
    const char* tod = (local_hour >= 5 && local_hour < 12) ? "MORNING"
                    : (local_hour < 17) ? "DAY"
                    : (local_hour < 21) ? "DUSK" : "NIGHT";
    const char* dom = (s->playfulCalm >= 20) ? "PLAYFUL"
                    : (s->playfulCalm <= -20) ? "CALM"
                    : (s->affectionIndep >= 20) ? "SWEET" : "CURIOUS";
    const char* mem = (m && m->lastAction[0]) ? m->lastAction : "resting";
    if (m && m->lastLearned[0]) {
        snprintf(out, out_len,
                 "[SP:%s] [ST:%s] [MD:%s] [TR:%s] [TM:%s] [MEM:%s] [LRN:%s]",
                 sp.slug,
                 sp.stageNames[s->stage < LIFE_STAGE_COUNT ? s->stage : 0],
                 zoo_mood_name(mood),
                 dom,
                 tod,
                 mem,
                 m->lastLearned);
    } else {
        snprintf(out, out_len,
                 "[SP:%s] [ST:%s] [MD:%s] [TR:%s] [TM:%s] [MEM:%s]",
                 sp.slug,
                 sp.stageNames[s->stage < LIFE_STAGE_COUNT ? s->stage : 0],
                 zoo_mood_name(mood),
                 dom,
                 tod,
                 mem);
    }
}
