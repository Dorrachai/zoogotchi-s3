#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// ZooGotchi S3 — 6 Categories × 3 Species Registry (18 Total)
//   Categories 0..4 (15 Real-World Zoo Animals): Authentic lifecycle + permadeath
//   Category 5      (3 Karimono Starters):       Classic Tamagotchi + Cross-breeding
// =============================================================================

enum class AnimalCategory : uint8_t {
    LAND     = 0,
    SKY      = 1,
    INSECT   = 2,
    WATER    = 3,
    REPTILE  = 4,
    KARIMONO = 5,
    COUNT    = 6,
};

enum class CircadianType : uint8_t {
    DIURNAL     = 0, // Active ~06:00–21:00
    NOCTURNAL   = 1, // Active ~19:00–07:00
    CREPUSCULAR = 2, // Active dawn (05–09) & dusk (17–22)
    CATHEMERAL  = 3, // Short rest cycles throughout day & night (e.g., Manatee)
};

static constexpr uint8_t SPECIES_PER_CATEGORY = 3;
static constexpr uint8_t TOTAL_SPECIES_COUNT  = 18;
static constexpr uint8_t LIFE_STAGE_COUNT     = 5; // 0..3 growing + 4 Elder

struct SpeciesProfile {
    uint8_t        id;
    AnimalCategory category;
    const char*    slug;
    const char*    name;
    const char*    scientificName;
    const char*    biomeName;
    CircadianType  circadian;
    const char*    stageNames[LIFE_STAGE_COUNT];
    uint16_t       stageHours[LIFE_STAGE_COUNT]; // cumulative hours to enter stage [0..4]
    uint16_t       naturalLifespanHours;         // for Real Animals (permadeath at old age)
    const char*    primaryFood;
    const char*    treatFood;
    const char*    envCareLabel;
    uint32_t       biomeBgHex;
    uint32_t       biomeAccentHex;
    uint32_t       bodyColorHex;
    const char*    funFacts[3];
    bool           isKarimono;
};

const char*           category_name(AnimalCategory cat);
const char*           circadian_name(CircadianType c);
const SpeciesProfile& species_get(uint8_t species_id);
uint8_t               species_id_for_cat(AnimalCategory cat, uint8_t index_in_cat);
bool                  species_is_sleep_hour(const SpeciesProfile& sp, uint8_t local_hour);
