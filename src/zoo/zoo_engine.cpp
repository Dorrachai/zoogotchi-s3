#include "zoo_engine.h"
#include "../config.h"
#include <string.h>
#include <stdio.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static const char* SAVE_PATH     = "/save.json";
static const char* SAVE_TMP      = "/save.tmp";
static const char* MEMORIAL_PATH = "/memorial.json";
static const char* MEMORIAL_TMP  = "/memorial.tmp";

static uint8_t clamp_u8(int v, int lo = 0, int hi = 100) {
    if (v < lo) return (uint8_t)lo;
    if (v > hi) return (uint8_t)hi;
    return (uint8_t)v;
}

static int8_t clamp_i8(int v, int lo = -100, int hi = 100) {
    if (v < lo) return (int8_t)lo;
    if (v > hi) return (int8_t)hi;
    return (int8_t)v;
}

void zoo_init_empty(ZooPetState* s) {
    memset(s, 0, sizeof(ZooPetState));
    strlcpy(s->keeperName, "Keeper", sizeof(s->keeperName));
    s->zooCoins = 40;
}

void zoo_adopt_species(ZooPetState* s, uint8_t species_id, const char* custom_name, int64_t now_epoch) {
    // Preserve cross-lifetime sanctuary data (keeperName, zooCoins, inventory, stepsTotal)
    char     kept_keeper[16];
    uint32_t kept_coins = s->zooCoins < 30 ? 30 : s->zooCoins;
    uint32_t kept_steps = s->stepsTotal;
    uint8_t  kept_inv[SHOP_ITEM_COUNT];
    memcpy(kept_keeper, s->keeperName, sizeof(kept_keeper));
    memcpy(kept_inv, s->inventory, sizeof(kept_inv));

    memset(s, 0, sizeof(ZooPetState));
    if (kept_keeper[0]) strlcpy(s->keeperName, kept_keeper, sizeof(s->keeperName));
    else strlcpy(s->keeperName, "Keeper", sizeof(s->keeperName));
    s->zooCoins   = kept_coins;
    s->stepsTotal = kept_steps;
    memcpy(s->inventory, kept_inv, sizeof(kept_inv));

    const SpeciesProfile& sp = species_get(species_id);
    s->hasActivePet = true;
    s->isDead       = false;
    s->deathCause   = DeathCause::NONE;
    s->speciesId    = sp.id;
    s->category     = sp.category;

    if (custom_name && custom_name[0]) {
        strlcpy(s->petName, custom_name, sizeof(s->petName));
    } else {
        strlcpy(s->petName, sp.name, sizeof(s->petName));
    }

    s->stage        = 0;
    s->ageSeconds   = 0;
    s->birthEpoch   = now_epoch;
    s->lastRtcEpoch = now_epoch;

    s->hunger     = 85;
    s->hydration  = 90;
    s->envCare    = 90;
    s->enrichment = 80;
    s->energy     = 90;
    s->health     = 100;
    s->bond       = 20;

    if (sp.isKarimono) {
        s->genome = karimono_starter_genome((uint8_t)(sp.id - 15));
        s->playfulCalm   = s->genome.traitBias[0];
        s->braveShy      = s->genome.traitBias[1];
        s->affectionIndep= s->genome.traitBias[2];
        s->curiousRoutine= s->genome.traitBias[3];

        LineageEntry le = {};
        le.generation   = s->genome.generation;
        le.genomeId     = s->genome.genomeId;
        le.primaryHue   = s->genome.primaryHue;
        le.accentHue    = s->genome.accentHue;
        le.bodyGene     = s->genome.bodyGene;
        le.earHornGene  = s->genome.earHornGene;
        le.tailWingGene = s->genome.tailWingGene;
        le.mutationMask = s->genome.mutationMask;
        le.bornEpoch    = now_epoch;
        strlcpy(le.name,    s->petName,            sizeof(le.name));
        strlcpy(le.parentA, s->genome.parentAName, sizeof(le.parentA));
        strlcpy(le.parentB, s->genome.parentBName, sizeof(le.parentB));
        lineage_append(le);
    }
}

static uint8_t compute_target_stage(const SpeciesProfile& sp, uint32_t age_sec) {
    uint32_t age_hr = age_sec / 3600UL;
    uint8_t st = 0;
    for (uint8_t i = 1; i < LIFE_STAGE_COUNT; i++) {
        if (age_hr >= sp.stageHours[i]) st = i;
    }
    return st;
}

static void step_vitals_minute(ZooPetState* s, const SpeciesProfile& sp, bool sleep_now, ZooTickEvents& ev) {
    // Sleeping drastically slows hunger/hydration/enrichment drain and restores energy
    if (sleep_now) {
        s->energy     = clamp_u8((int)s->energy + 4);
        s->hunger     = clamp_u8((int)s->hunger - 1);
        s->hydration  = clamp_u8((int)s->hydration - 1);
    } else {
        s->hunger     = clamp_u8((int)s->hunger - 2);
        s->hydration  = clamp_u8((int)s->hydration - 2);
        s->envCare    = clamp_u8((int)s->envCare - 1);
        s->enrichment = clamp_u8((int)s->enrichment - 2);
        s->energy     = clamp_u8((int)s->energy - 1);
    }

    // Check critical neglect -> sickness
    bool low_care = (s->hunger <= 15 || s->hydration <= 15 || s->envCare <= 15);
    if (low_care) {
        s->_sickAccum += 60;
        if (s->_sickAccum >= 600 && !s->sick) { // 10 min critical neglect
            s->sick = true;
            s->careMistakes++;
            ev.becameSick = true;
        }
        s->health = clamp_u8((int)s->health - (s->sick ? 2 : 1));
    } else if (s->sick) {
        s->health = clamp_u8((int)s->health - 1);
    } else {
        s->_sickAccum = 0;
        if (s->hunger > 55 && s->hydration > 55 && s->envCare > 55 && s->health < 100) {
            s->health = clamp_u8((int)s->health + 1);
        }
    }

    if (s->hunger <= 20 || s->hydration <= 20 || s->envCare <= 20 || s->sick) {
        ev.alertNeeded = true;
    }
}

ZooTickEvents zoo_tick_1s(ZooPetState* s, int64_t now_epoch, uint8_t local_hour, bool demo_speed, uint32_t now_ms) {
    ZooTickEvents ev = {};
    if (!s->hasActivePet || s->isDead) return ev;

    const SpeciesProfile& sp = species_get(s->speciesId);
    uint32_t sim_step = demo_speed ? 30UL : 1UL;
    s->ageSeconds += sim_step;
    s->lastRtcEpoch = now_epoch;

    // Circadian state
    bool natural_sleep = species_is_sleep_hour(sp, local_hour);
    s->sleeping = s->manualRest || natural_sleep;
    if (!natural_sleep && s->energy >= 95) s->manualRest = false;

    // Stage progression
    uint8_t next_stage = compute_target_stage(sp, s->ageSeconds);
    if (next_stage > s->stage) {
        s->stage = next_stage;
        ev.stageAdvanced = true;
        ev.newStage = next_stage;
    }

    // Accumulate 60 sim-seconds per minute vital update
    s->_secAccum += (uint16_t)sim_step;
    while (s->_secAccum >= 60) {
        s->_secAccum -= 60;
        step_vitals_minute(s, sp, s->sleeping, ev);
    }

    // Real Animal Permadeath Rules (Category 0..4)
    if (!sp.isKarimono) {
        uint32_t max_age_sec = (uint32_t)sp.naturalLifespanHours * 3600UL;
        if (sp.naturalLifespanHours > 0 && s->ageSeconds >= max_age_sec) {
            s->isDead     = true;
            s->deathCause = DeathCause::NATURAL_OLD_AGE;
            ev.died       = true;
            memorial_record(s, now_epoch);
            return ev;
        }
        if (s->health == 0) {
            s->isDead     = true;
            s->deathCause = DeathCause::NEGLECT_ILLNESS;
            ev.died       = true;
            memorial_record(s, now_epoch);
            return ev;
        }
    } else {
        // Karimono never suffers unfair permadeath: at 0 health it enters fatigued rest until healed
        if (s->health == 0) {
            s->health = 10;
            s->sick   = true;
            ev.alertNeeded = true;
        }
    }

    (void)now_ms;
    return ev;
}

ZooTickEvents zoo_catchup_elapsed(ZooPetState* s, int64_t now_epoch, int16_t tz_offset_min, bool demo_speed) {
    ZooTickEvents total_ev = {};
    if (!s->hasActivePet || s->isDead || s->lastRtcEpoch <= 0) {
        s->lastRtcEpoch = now_epoch;
        return total_ev;
    }

    int64_t delta = now_epoch - s->lastRtcEpoch;
    if (delta <= 1) {
        s->lastRtcEpoch = now_epoch;
        return total_ev;
    }
    // Cap offline catchup to 72 hours so battery removal for weeks doesn't jump unrealistically
    if (delta > 72L * 3600L) delta = 72L * 3600L;

    const SpeciesProfile& sp = species_get(s->speciesId);
    // Simulate in 15-minute buckets for accurate circadian catchup
    int64_t cursor = s->lastRtcEpoch;
    while (delta > 0 && !s->isDead) {
        int64_t chunk = delta > 900 ? 900 : delta;
        int64_t local_t = cursor + (int64_t)tz_offset_min * 60L;
        uint8_t hr = (uint8_t)(((local_t / 3600L) % 24 + 24) % 24);
        bool asleep = species_is_sleep_hour(sp, hr);

        uint32_t sim_s = (uint32_t)chunk * (demo_speed ? 10UL : 1UL);
        s->ageSeconds += sim_s;

        // Gentle offline vitals curve: safe sleep overnight, realistic daytime hunger
        int mins = (int)(sim_s / 60);
        if (mins < 1) mins = 1;
        int steps15 = (mins + 14) / 15;
        for (int k = 0; k < steps15; k++) {
            ZooTickEvents ev = {};
            step_vitals_minute(s, sp, asleep, ev);
            if (ev.becameSick) total_ev.becameSick = true;
        }

        // Prevent offline sleeping overnight from executing an unexpected instant neglect death
        if (!sp.isKarimono && s->ageSeconds >= (uint32_t)sp.naturalLifespanHours * 3600UL) {
            s->isDead     = true;
            s->deathCause = DeathCause::NATURAL_OLD_AGE;
            total_ev.died = true;
            memorial_record(s, cursor + chunk);
            break;
        }
        if (!sp.isKarimono && s->health < 15 && asleep) {
            s->health = 15; // wakes owner up with alert instead of silent overnight wipeout
            total_ev.alertNeeded = true;
        } else if (!sp.isKarimono && s->health == 0) {
            s->isDead     = true;
            s->deathCause = DeathCause::NEGLECT_ILLNESS;
            total_ev.died = true;
            memorial_record(s, cursor + chunk);
            break;
        }

        uint8_t st = compute_target_stage(sp, s->ageSeconds);
        if (st > s->stage) {
            s->stage = st;
            total_ev.stageAdvanced = true;
            total_ev.newStage = st;
        }

        cursor += chunk;
        delta  -= chunk;
    }

    s->lastRtcEpoch = now_epoch;
    return total_ev;
}

bool zoo_perform_action(ZooPetState* s, ZooAction act, char* out_feedback, uint16_t fb_len) {
    if (!s->hasActivePet || s->isDead) return false;
    const SpeciesProfile& sp = species_get(s->speciesId);

    auto msg = [&](const char* txt) {
        if (out_feedback && fb_len > 0) strlcpy(out_feedback, txt, fb_len);
    };

    switch (act) {
    case ZooAction::FEED_PRIMARY:
        s->hunger     = clamp_u8((int)s->hunger + 28);
        s->hydration  = clamp_u8((int)s->hydration + 10);
        s->bond       = clamp_u8((int)s->bond + 2);
        s->curiousRoutine = clamp_i8((int)s->curiousRoutine - 2);
        if (out_feedback && fb_len) snprintf(out_feedback, fb_len, "Ate %s!", sp.primaryFood);
        return true;

    case ZooAction::FEED_TREAT:
        s->hunger     = clamp_u8((int)s->hunger + 14);
        s->enrichment = clamp_u8((int)s->enrichment + 18);
        s->bond       = clamp_u8((int)s->bond + 4);
        s->playfulCalm    = clamp_i8((int)s->playfulCalm + 2);
        s->curiousRoutine = clamp_i8((int)s->curiousRoutine + 3);
        if (out_feedback && fb_len) snprintf(out_feedback, fb_len, "Loved %s!", sp.treatFood);
        return true;

    case ZooAction::ENV_CARE:
        s->envCare    = clamp_u8((int)s->envCare + 35);
        s->hydration  = clamp_u8((int)s->hydration + 25);
        s->health     = clamp_u8((int)s->health + 4);
        s->bond       = clamp_u8((int)s->bond + 2);
        if (out_feedback && fb_len) snprintf(out_feedback, fb_len, "%s refreshed!", sp.envCareLabel);
        return true;

    case ZooAction::VET_MEDICINE:
        if (!s->sick && s->health >= 95) {
            msg("Already healthy!");
            return false;
        }
        s->sick   = false;
        s->health = clamp_u8((int)s->health + 38);
        s->bond   = clamp_u8((int)s->bond + 3);
        msg("Feeling much better!");
        return true;

    case ZooAction::TOGGLE_SLEEP:
        s->manualRest = !s->manualRest;
        s->sleeping   = s->manualRest;
        msg(s->manualRest ? "Cozy nap time..." : "Wide awake!");
        return true;

    case ZooAction::PET_STROKE:
        s->enrichment     = clamp_u8((int)s->enrichment + 10);
        s->bond           = clamp_u8((int)s->bond + 4);
        s->playfulCalm    = clamp_i8((int)s->playfulCalm - 3);
        s->affectionIndep = clamp_i8((int)s->affectionIndep + 4);
        msg("Gentle pets!");
        return true;

    case ZooAction::ROCK_LULLABY:
        s->energy         = clamp_u8((int)s->energy + 12);
        s->bond           = clamp_u8((int)s->bond + 3);
        s->playfulCalm    = clamp_i8((int)s->playfulCalm - 4);
        s->affectionIndep = clamp_i8((int)s->affectionIndep + 2);
        msg("Soothed by rocking!");
        return true;
    }
    return false;
}

void zoo_apply_minigame_result(ZooPetState* s, uint16_t score, uint16_t coins_earned, int8_t bravery_delta) {
    if (!s->hasActivePet || s->isDead) return;
    s->enrichment     = clamp_u8((int)s->enrichment + 24 + (int)(score / 10));
    s->bond           = clamp_u8((int)s->bond + 5);
    s->energy         = clamp_u8((int)s->energy - 8);
    s->zooCoins      += coins_earned;
    s->playfulCalm    = clamp_i8((int)s->playfulCalm + 5);
    s->braveShy       = clamp_i8((int)s->braveShy + bravery_delta);
    s->curiousRoutine = clamp_i8((int)s->curiousRoutine + 2);
}

void zoo_apply_study_reward(ZooPetState* s, uint16_t coins, uint8_t enrich_boost, uint8_t bond_boost, int8_t curious_delta) {
    if (!s) return;
    s->zooCoins += coins;
    if (!s->hasActivePet || s->isDead) return;
    s->enrichment     = clamp_u8((int)s->enrichment + (int)enrich_boost);
    s->bond           = clamp_u8((int)s->bond + (int)bond_boost);
    s->curiousRoutine = clamp_i8((int)s->curiousRoutine + curious_delta);
}

void zoo_apply_steps(ZooPetState* s, uint32_t new_steps) {
    if (new_steps == 0) return;
    s->stepsToday += new_steps;
    s->stepsTotal += new_steps;
    if (!s->hasActivePet || s->isDead) return;

    // Every 25 steps gives +1 coin and small enrichment/bravery boost
    uint32_t prev_blocks = (s->stepsToday - new_steps) / 25UL;
    uint32_t cur_blocks  = s->stepsToday / 25UL;
    if (cur_blocks > prev_blocks) {
        uint32_t gained = cur_blocks - prev_blocks;
        s->zooCoins  += gained;
        s->enrichment = clamp_u8((int)s->enrichment + (int)gained * 2);
        s->braveShy   = clamp_i8((int)s->braveShy + (int)gained);
    }
}

void zoo_trigger_shake(ZooPetState* s, bool hard, uint32_t now_ms) {
    if (!s->hasActivePet || s->isDead) return;
    if (hard) {
        s->dizzyUntilMs = now_ms + DIZZY_DURATION_MS;
        s->braveShy     = clamp_i8((int)s->braveShy - 2);
    } else {
        s->enrichment   = clamp_u8((int)s->enrichment + 5);
        s->playfulCalm  = clamp_i8((int)s->playfulCalm + 1);
    }
}

bool zoo_shop_buy_or_use(ZooPetState* s, uint8_t item_idx, char* out_msg, uint16_t msg_len) {
    if (item_idx >= SHOP_ITEM_COUNT || !s->hasActivePet || s->isDead) return false;
    const ShopItem& it = shop_get_item(item_idx);

    if (it.isAccessory) {
        if (s->inventory[item_idx] > 0) {
            s->equippedAccessory = (s->equippedAccessory == item_idx) ? 0 : item_idx;
            if (out_msg && msg_len) snprintf(out_msg, msg_len,
                s->equippedAccessory ? "Equipped %s!" : "Removed %s", it.name);
            return true;
        }
        if (s->zooCoins < it.price) {
            if (out_msg && msg_len) snprintf(out_msg, msg_len, "Need %u coins!", it.price);
            return false;
        }
        s->zooCoins -= it.price;
        s->inventory[item_idx] = 1;
        s->equippedAccessory   = item_idx;
        if (out_msg && msg_len) snprintf(out_msg, msg_len, "Bought & equipped!");
        return true;
    }

    if (s->zooCoins < it.price) {
        if (out_msg && msg_len) snprintf(out_msg, msg_len, "Need %u coins!", it.price);
        return false;
    }
    s->zooCoins -= it.price;
    switch (it.id) {
    case ShopItemId::GOURMET_FEAST:
        s->hunger     = clamp_u8((int)s->hunger + 45);
        s->enrichment = clamp_u8((int)s->enrichment + 15);
        s->bond       = clamp_u8((int)s->bond + 5);
        break;
    case ShopItemId::BIOME_TOY:
        s->enrichment = clamp_u8((int)s->enrichment + 40);
        s->bond       = clamp_u8((int)s->bond + 8);
        s->curiousRoutine = clamp_i8((int)s->curiousRoutine + 4);
        break;
    case ShopItemId::VITAMIN_TONIC:
        s->sick   = false;
        s->health = clamp_u8((int)s->health + 40);
        break;
    case ShopItemId::HABITAT_KIT:
        s->envCare   = clamp_u8((int)s->envCare + 50);
        s->hydration = clamp_u8((int)s->hydration + 40);
        break;
    default: break;
    }
    if (out_msg && msg_len) snprintf(out_msg, msg_len, "Used %s!", it.name);
    return true;
}

bool zoo_karimono_can_breed(const ZooPetState* s) {
    if (!s || !s->hasActivePet || s->isDead) return false;
    const SpeciesProfile& sp = species_get(s->speciesId);
    return sp.isKarimono && s->stage >= 3; // Adult or Grand
}

bool zoo_karimono_crossbreed(ZooPetState* s, const KarimonoGenome& partner, const char* partner_name,
                             const char* child_name, int64_t now_epoch, uint32_t seed) {
    if (!zoo_karimono_can_breed(s)) return false;

    KarimonoGenome child = karimono_breed(s->genome, s->petName, partner, partner_name, seed);

    s->genome       = child;
    s->stage        = 0; // New Karimono Egg!
    s->ageSeconds   = 0;
    s->birthEpoch   = now_epoch;
    s->lastRtcEpoch = now_epoch;
    s->hunger       = 88;
    s->hydration    = 90;
    s->envCare      = 92;
    s->enrichment   = 85;
    s->energy       = 95;
    s->health       = 100;
    s->bond         = 30;
    s->sick         = false;
    s->sleeping     = false;
    s->manualRest   = false;
    s->careMistakes = 0;

    s->playfulCalm    = child.traitBias[0];
    s->braveShy       = child.traitBias[1];
    s->affectionIndep = child.traitBias[2];
    s->curiousRoutine = child.traitBias[3];

    if (child_name && child_name[0]) {
        strlcpy(s->petName, child_name, sizeof(s->petName));
    } else {
        snprintf(s->petName, sizeof(s->petName), "Kari-G%u", (unsigned)child.generation);
    }

    LineageEntry le = {};
    le.generation   = child.generation;
    le.genomeId     = child.genomeId;
    le.primaryHue   = child.primaryHue;
    le.accentHue    = child.accentHue;
    le.bodyGene     = child.bodyGene;
    le.earHornGene  = child.earHornGene;
    le.tailWingGene = child.tailWingGene;
    le.mutationMask = child.mutationMask;
    le.bornEpoch    = now_epoch;
    strlcpy(le.name,    s->petName,         sizeof(le.name));
    strlcpy(le.parentA, child.parentAName,  sizeof(le.parentA));
    strlcpy(le.parentB, child.parentBName,  sizeof(le.parentB));
    lineage_append(le);
    return true;
}

PetMood zoo_compute_mood(const ZooPetState* s, uint32_t now_ms) {
    if (!s || !s->hasActivePet || s->isDead) return PetMood::MEMORIAL;
    if (s->dizzyUntilMs > now_ms) return PetMood::DIZZY;
    if (s->sick)                  return PetMood::SICK;
    if (s->sleeping)              return PetMood::SLEEPING;
    if (s->hunger <= 25 || s->hydration <= 25) return PetMood::HUNGRY;
    if (s->enrichment <= 25)      return PetMood::LONELY;
    if (s->hunger >= 75 && s->enrichment >= 75 && s->bond >= 50) return PetMood::ECSTATIC;
    return PetMood::CONTENT;
}

const char* zoo_mood_name(PetMood m) {
    switch (m) {
    case PetMood::CONTENT:  return "Calm";
    case PetMood::ECSTATIC: return "Happy";
    case PetMood::SLEEPING: return "Asleep";
    case PetMood::HUNGRY:   return "Hungry";
    case PetMood::SICK:     return "Unwell";
    case PetMood::DIZZY:    return "Dizzy";
    case PetMood::LONELY:   return "Bored";
    case PetMood::MEMORIAL: return "Memorial";
    default:                return "Calm";
    }
}

uint16_t zoo_get_chameleon_hue(const ZooPetState* s) {
    if (!s) return 140;
    if (s->sick)             return 275; // muted plum
    if (s->sleeping)         return 190; // sleepy teal
    if (s->enrichment >= 75) return 125; // bright emerald
    if (s->hunger <= 25)     return 34;  // warm amber
    return 155;
}

bool zoo_save_state(const ZooPetState* s) {
    if (!s) return false;
    JsonDocument doc;
    doc["active"] = s->hasActivePet;
    doc["dead"]   = s->isDead;
    doc["cause"]  = (uint8_t)s->deathCause;
    doc["sp"]     = s->speciesId;
    doc["cat"]    = (uint8_t)s->category;
    doc["pname"]  = s->petName;
    doc["kname"]  = s->keeperName;
    doc["stage"]  = s->stage;
    doc["age"]    = s->ageSeconds;
    doc["birth"]  = s->birthEpoch;
    doc["rtc"]    = s->lastRtcEpoch;

    doc["hun"] = s->hunger;
    doc["hyd"] = s->hydration;
    doc["env"] = s->envCare;
    doc["enr"] = s->enrichment;
    doc["eng"] = s->energy;
    doc["hlt"] = s->health;
    doc["bnd"] = s->bond;

    doc["t0"] = s->playfulCalm;
    doc["t1"] = s->braveShy;
    doc["t2"] = s->affectionIndep;
    doc["t3"] = s->curiousRoutine;

    doc["sick"]   = s->sick;
    doc["rest"]   = s->manualRest;
    doc["miss"]   = s->careMistakes;
    doc["coins"]  = s->zooCoins;
    doc["st_day"] = s->stepsToday;
    doc["st_tot"] = s->stepsTotal;
    doc["acc"]    = s->equippedAccessory;

    JsonArray inv = doc["inv"].to<JsonArray>();
    for (uint8_t i = 0; i < SHOP_ITEM_COUNT; i++) inv.add(s->inventory[i]);

    JsonObject g = doc["g"].to<JsonObject>();
    g["gen"]  = s->genome.generation;
    g["body"] = s->genome.bodyGene;
    g["ear"]  = s->genome.earHornGene;
    g["eye"]  = s->genome.eyeGene;
    g["tail"] = s->genome.tailWingGene;
    g["pat"]  = s->genome.patternGene;
    g["ph"]   = s->genome.primaryHue;
    g["ah"]   = s->genome.accentHue;
    g["mut"]  = s->genome.mutationMask;
    g["id"]   = s->genome.genomeId;
    g["pa"]   = s->genome.parentAName;
    g["pb"]   = s->genome.parentBName;

    File f = LittleFS.open(SAVE_TMP, "w");
    if (!f) return false;
    size_t n = serializeJson(doc, f);
    f.close();
    if (n == 0) return false;
    if (LittleFS.exists(SAVE_PATH)) LittleFS.remove(SAVE_PATH);
    return LittleFS.rename(SAVE_TMP, SAVE_PATH);
}

bool zoo_load_state(ZooPetState* s) {
    zoo_init_empty(s);
    if (!LittleFS.exists(SAVE_PATH)) return false;
    File f = LittleFS.open(SAVE_PATH, "r");
    if (!f) return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    s->hasActivePet = doc["active"] | false;
    s->isDead       = doc["dead"]   | false;
    s->deathCause   = (DeathCause)(doc["cause"] | 0);
    s->speciesId    = doc["sp"]     | 0;
    s->category     = species_get(s->speciesId).category;
    strlcpy(s->petName,    doc["pname"] | "Pet",    sizeof(s->petName));
    strlcpy(s->keeperName, doc["kname"] | "Keeper", sizeof(s->keeperName));

    s->stage        = doc["stage"] | 0;
    s->ageSeconds   = doc["age"]   | 0UL;
    s->birthEpoch   = doc["birth"] | 0LL;
    s->lastRtcEpoch = doc["rtc"]   | 0LL;

    s->hunger     = doc["hun"] | 80;
    s->hydration  = doc["hyd"] | 80;
    s->envCare    = doc["env"] | 80;
    s->enrichment = doc["enr"] | 80;
    s->energy     = doc["eng"] | 80;
    s->health     = doc["hlt"] | 100;
    s->bond       = doc["bnd"] | 20;

    s->playfulCalm    = (int8_t)(doc["t0"] | 0);
    s->braveShy       = (int8_t)(doc["t1"] | 0);
    s->affectionIndep = (int8_t)(doc["t2"] | 0);
    s->curiousRoutine = (int8_t)(doc["t3"] | 0);

    s->sick              = doc["sick"]   | false;
    s->manualRest        = doc["rest"]   | false;
    s->careMistakes      = doc["miss"]   | 0;
    s->zooCoins          = doc["coins"]  | 40UL;
    s->stepsToday        = doc["st_day"] | 0UL;
    s->stepsTotal        = doc["st_tot"] | 0UL;
    s->equippedAccessory = doc["acc"]    | 0;

    if (doc["inv"].is<JsonArray>()) {
        uint8_t idx = 0;
        for (JsonVariant v : doc["inv"].as<JsonArray>()) {
            if (idx >= SHOP_ITEM_COUNT) break;
            s->inventory[idx++] = (uint8_t)(v | 0);
        }
    }

    if (doc["g"].is<JsonObject>()) {
        JsonObject g = doc["g"];
        s->genome.generation   = g["gen"]  | 1;
        s->genome.bodyGene     = g["body"] | 0;
        s->genome.earHornGene  = g["ear"]  | 0;
        s->genome.eyeGene      = g["eye"]  | 0;
        s->genome.tailWingGene = g["tail"] | 0;
        s->genome.patternGene  = g["pat"]  | 0;
        s->genome.primaryHue   = g["ph"]   | 40;
        s->genome.accentHue    = g["ah"]   | 200;
        s->genome.mutationMask = g["mut"]  | 0;
        s->genome.genomeId     = g["id"]   | 0x4B415231UL;
        strlcpy(s->genome.parentAName, g["pa"] | "Origin", sizeof(s->genome.parentAName));
        strlcpy(s->genome.parentBName, g["pb"] | "Origin", sizeof(s->genome.parentBName));
    }
    return true;
}

bool memorial_record(const ZooPetState* s, int64_t died_epoch) {
    if (!s) return false;
    MemorialEntry buf[MEMORIAL_MAX_ENTRIES];
    uint8_t cnt = memorial_load(buf, MEMORIAL_MAX_ENTRIES);
    if (cnt >= MEMORIAL_MAX_ENTRIES) {
        memmove(&buf[0], &buf[1], sizeof(MemorialEntry) * (MEMORIAL_MAX_ENTRIES - 1));
        cnt = MEMORIAL_MAX_ENTRIES - 1;
    }
    MemorialEntry& m = buf[cnt++];
    memset(&m, 0, sizeof(m));
    m.speciesId  = s->speciesId;
    m.cause      = s->deathCause;
    m.livedHours = s->ageSeconds / 3600UL;
    m.maxStage   = s->stage;
    m.finalBond  = s->bond;
    m.diedEpoch  = died_epoch;
    strlcpy(m.petName, s->petName, sizeof(m.petName));

    JsonDocument doc;
    JsonArray arr = doc["memorials"].to<JsonArray>();
    for (uint8_t i = 0; i < cnt; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["sp"]    = buf[i].speciesId;
        o["cause"] = (uint8_t)buf[i].cause;
        o["hrs"]   = buf[i].livedHours;
        o["st"]    = buf[i].maxStage;
        o["bond"]  = buf[i].finalBond;
        o["name"]  = buf[i].petName;
        o["ts"]    = buf[i].diedEpoch;
    }

    File f = LittleFS.open(MEMORIAL_TMP, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    if (LittleFS.exists(MEMORIAL_PATH)) LittleFS.remove(MEMORIAL_PATH);
    return LittleFS.rename(MEMORIAL_TMP, MEMORIAL_PATH);
}

uint8_t memorial_load(MemorialEntry* out_arr, uint8_t max_count) {
    if (!out_arr || max_count == 0 || !LittleFS.exists(MEMORIAL_PATH)) return 0;
    File f = LittleFS.open(MEMORIAL_PATH, "r");
    if (!f) return 0;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err || !doc["memorials"].is<JsonArray>()) return 0;

    uint8_t n = 0;
    for (JsonObject o : doc["memorials"].as<JsonArray>()) {
        if (n >= max_count) break;
        MemorialEntry& m = out_arr[n++];
        memset(&m, 0, sizeof(m));
        m.speciesId  = o["sp"]    | 0;
        m.cause      = (DeathCause)(o["cause"] | 1);
        m.livedHours = o["hrs"]   | 0UL;
        m.maxStage   = o["st"]    | 0;
        m.finalBond  = o["bond"]  | 0;
        m.diedEpoch  = o["ts"]    | 0LL;
        strlcpy(m.petName, o["name"] | "Friend", sizeof(m.petName));
    }
    return n;
}

uint8_t memorial_count() {
    MemorialEntry tmp[MEMORIAL_MAX_ENTRIES];
    return memorial_load(tmp, MEMORIAL_MAX_ENTRIES);
}
