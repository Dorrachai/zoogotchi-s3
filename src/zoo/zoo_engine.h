#pragma once
#include "species_registry.h"
#include "karimono_genetics.h"
#include "economy_shop.h"
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// =============================================================================
// ZooGotchi S3 — Core Real-Zoo Lifecycle, Permadeath & Karimono Engine
// =============================================================================

static constexpr uint8_t MEMORIAL_MAX_ENTRIES = 32;

enum class DeathCause : uint8_t {
    NONE            = 0,
    NATURAL_OLD_AGE = 1,
    NEGLECT_ILLNESS = 2,
};

enum class PetMood : uint8_t {
    CONTENT   = 0,
    ECSTATIC  = 1,
    SLEEPING  = 2,
    HUNGRY    = 3,
    SICK      = 4,
    DIZZY     = 5,
    LONELY    = 6,
    MEMORIAL  = 7,
};

enum class ZooAction : uint8_t {
    FEED_PRIMARY = 0,
    FEED_TREAT   = 1,
    ENV_CARE     = 2,
    VET_MEDICINE = 3,
    TOGGLE_SLEEP = 4,
    PET_STROKE   = 5,
    ROCK_LULLABY = 6,
};

struct ZooPetState {
    bool           hasActivePet;
    bool           isDead;
    DeathCause     deathCause;
    uint8_t        speciesId;
    AnimalCategory category;
    char           petName[16];
    char           keeperName[16];

    // Lifecycle & RTC
    uint8_t        stage;          // 0..4
    uint32_t       ageSeconds;
    int64_t        birthEpoch;
    int64_t        lastRtcEpoch;

    // 7 Vitals (0..100)
    uint8_t        hunger;
    uint8_t        hydration;
    uint8_t        envCare;
    uint8_t        enrichment;
    uint8_t        energy;
    uint8_t        health;
    uint8_t        bond;

    // 4 Evolving Personality Axes (-100..+100)
    int8_t         playfulCalm;    // + = Playful, - = Calm
    int8_t         braveShy;       // + = Brave,   - = Shy
    int8_t         affectionIndep; // + = Affectionate, - = Independent
    int8_t         curiousRoutine; // + = Curious, - = Routine

    // Runtime Status
    bool           sick;
    bool           sleeping;
    bool           manualRest;
    uint32_t       dizzyUntilMs;
    uint32_t       lastPetHeartMs;
    uint16_t       careMistakes;
    uint16_t       careStreakDays;

    // Economy, Steps & Accessories
    uint32_t       zooCoins;
    uint32_t       stepsToday;
    uint32_t       stepsTotal;
    uint8_t        inventory[SHOP_ITEM_COUNT];
    uint8_t        equippedAccessory; // 0 = none, 4..9 = ShopItemId

    // Karimono Cross-Breed Genome (valid when category == KARIMONO)
    KarimonoGenome genome;

    // Internal decay accumulators (seconds)
    uint16_t       _secAccum;
    uint16_t       _sickAccum;
    uint16_t       _streakAccum;
};

struct MemorialEntry {
    uint8_t    speciesId;
    DeathCause cause;
    uint32_t   livedHours;
    uint8_t    maxStage;
    uint8_t    finalBond;
    char       petName[16];
    int64_t    diedEpoch;
};

struct ZooTickEvents {
    bool stageAdvanced;
    bool becameSick;
    bool died;
    bool alertNeeded;
    uint8_t newStage;
};

void zoo_init_empty(ZooPetState* s);
void zoo_adopt_species(ZooPetState* s, uint8_t species_id, const char* custom_name, int64_t now_epoch);

ZooTickEvents zoo_tick_1s(ZooPetState* s, int64_t now_epoch, uint8_t local_hour, bool demo_speed, uint32_t now_ms);
ZooTickEvents zoo_catchup_elapsed(ZooPetState* s, int64_t now_epoch, int16_t tz_offset_min, bool demo_speed);

bool zoo_perform_action(ZooPetState* s, ZooAction act, char* out_feedback, uint16_t fb_len);
void zoo_apply_minigame_result(ZooPetState* s, uint16_t score, uint16_t coins_earned, int8_t bravery_delta);
void zoo_apply_study_reward(ZooPetState* s, uint16_t coins, uint8_t enrich_boost, uint8_t bond_boost, int8_t curious_delta);
void zoo_apply_steps(ZooPetState* s, uint32_t new_steps);
void zoo_trigger_shake(ZooPetState* s, bool hard, uint32_t now_ms);

bool zoo_shop_buy_or_use(ZooPetState* s, uint8_t item_idx, char* out_msg, uint16_t msg_len);
bool zoo_karimono_can_breed(const ZooPetState* s);
bool zoo_karimono_crossbreed(ZooPetState* s, const KarimonoGenome& partner, const char* partner_name,
                             const char* child_name, int64_t now_epoch, uint32_t seed);

PetMood     zoo_compute_mood(const ZooPetState* s, uint32_t now_ms);
const char* zoo_mood_name(PetMood m);
uint16_t    zoo_get_chameleon_hue(const ZooPetState* s);

// Atomic LittleFS Persistence
bool    zoo_save_state(const ZooPetState* s);
bool    zoo_load_state(ZooPetState* s);
bool    memorial_record(const ZooPetState* s, int64_t died_epoch);
uint8_t memorial_load(MemorialEntry* out_arr, uint8_t max_count);
uint8_t memorial_count();
