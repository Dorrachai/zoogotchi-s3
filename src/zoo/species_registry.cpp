#include "species_registry.h"
#include "categories/land.h"
#include "categories/sky.h"
#include "categories/insect.h"
#include "categories/water.h"
#include "categories/reptile.h"
#include "categories/karimono.h"

static const SpeciesProfile* const kCategoryTables[(uint8_t)AnimalCategory::COUNT] = {
    kCategoryLand,
    kCategorySky,
    kCategoryInsect,
    kCategoryWater,
    kCategoryReptile,
    kCategoryKarimono,
};

const char* category_name(AnimalCategory cat) {
    switch (cat) {
    case AnimalCategory::LAND:     return "Land";
    case AnimalCategory::SKY:      return "Sky";
    case AnimalCategory::INSECT:   return "Insect";
    case AnimalCategory::WATER:    return "Water";
    case AnimalCategory::REPTILE:  return "Reptile";
    case AnimalCategory::KARIMONO: return "Karimono";
    default:                       return "Zoo";
    }
}

const char* circadian_name(CircadianType c) {
    switch (c) {
    case CircadianType::DIURNAL:     return "Diurnal";
    case CircadianType::NOCTURNAL:   return "Nocturnal";
    case CircadianType::CREPUSCULAR: return "Crepuscular";
    case CircadianType::CATHEMERAL:  return "Cathemeral";
    default:                         return "Diurnal";
    }
}

const SpeciesProfile& species_get(uint8_t species_id) {
    if (species_id >= TOTAL_SPECIES_COUNT) return kCategoryLand[0];
    return kCategoryTables[species_id / SPECIES_PER_CATEGORY][species_id % SPECIES_PER_CATEGORY];
}

uint8_t species_id_for_cat(AnimalCategory cat, uint8_t index_in_cat) {
    uint8_t base = (uint8_t)cat * SPECIES_PER_CATEGORY;
    if (index_in_cat >= SPECIES_PER_CATEGORY) index_in_cat = 0;
    uint8_t id = (uint8_t)(base + index_in_cat);
    return id < TOTAL_SPECIES_COUNT ? id : 0;
}

bool species_is_sleep_hour(const SpeciesProfile& sp, uint8_t h) {
    h %= 24;
    switch (sp.circadian) {
    case CircadianType::DIURNAL:
        return (h >= 21 || h < 6);
    case CircadianType::NOCTURNAL:
        return (h >= 8 && h < 19);
    case CircadianType::CREPUSCULAR:
        // Rests midday (11..15) and deep night (23..4)
        return ((h >= 11 && h < 15) || h >= 23 || h < 4);
    case CircadianType::CATHEMERAL:
        // Gentle periodic rest naps (e.g., 1..4 and 13..15)
        return ((h >= 1 && h < 4) || (h >= 13 && h < 15));
    }
    return false;
}
