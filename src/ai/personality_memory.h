#pragma once
#include "../zoo/zoo_engine.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// Episodic Memory & Diary (/littlefs/memory.json, <= 16 KB)
// Injects <= 48-token structured context into every PetBrain interaction.
// =============================================================================

static constexpr uint8_t DIARY_MAX_ENTRIES = 20;

struct DiaryEntry {
    int64_t tsEpoch;
    char    text[72];
};

struct PetMemory {
    uint32_t   totalMeals;
    uint32_t   totalPets;
    uint32_t   totalGames;
    uint32_t   totalChats;
    char       favoriteFood[24];
    char       lastAction[24];
    char       lastLearned[48];
    uint8_t    diaryCount;
    DiaryEntry diary[DIARY_MAX_ENTRIES];
};

void memory_init(PetMemory* m);
bool memory_load(PetMemory* m);
bool memory_save(const PetMemory* m);
void memory_record_event(PetMemory* m, const char* event_text, int64_t epoch);
void memory_build_prompt_context(const ZooPetState* s, const PetMemory* m,
                                 uint8_t local_hour, char* out, size_t out_len);
