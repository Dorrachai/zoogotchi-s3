#pragma once
#include "../species_registry.h"

// Real-Zoo Category 4: REPTILE (Permadeath)
static const SpeciesProfile kCategoryReptile[SPECIES_PER_CATEGORY] = {
    {
        12, AnimalCategory::REPTILE, "tortoise", "Galapagos Tortoise", "Chelonoidis niger",
        "Volcanic Meadow", CircadianType::DIURNAL,
        {"Egg", "Hatchling", "Juvenile", "Adult", "Ancient"},
        {0, 5, 36, 120, 260}, 420,
        "Cactus Pad", "Dandelion Greens", "Basking Warmth",
        0xEFECE1, 0x65A30D, 0x6B7250,
        {
            "Galapagos tortoises can live well over a century!",
            "Warm morning sunshine powers their slow, peaceful walks.",
            "They rest comfortably for up to 16 hours after grazing."
        },
        false
    },
    {
        13, AnimalCategory::REPTILE, "chameleon", "Veiled Chameleon", "Chamaeleo calyptratus",
        "Misty Leaves", CircadianType::DIURNAL,
        {"Egg", "Hatchling", "Juvenile", "Adult", "Elder"},
        {0, 3, 20, 62, 150}, 210,
        "Leaf Cricket", "Collard Leaf", "Leaf Mist",
        0xE4F3E5, 0x15803D, 0x52B788,
        {
            "Chameleon eyes swivel independently in two directions at once!",
            "Color shifts show mood, warmth, and cozy comfort.",
            "They sip glistening morning dew drops right off leaves."
        },
        false
    },
    {
        14, AnimalCategory::REPTILE, "leopard_gecko", "Leopard Gecko", "Eublepharis macularius",
        "Rocky Oasis", CircadianType::CREPUSCULAR,
        {"Egg", "Hatchling", "Juvenile", "Adult", "Elder"},
        {0, 3, 20, 60, 160}, 240,
        "Mealworm", "Waxworm Treat", "Warm Rock",
        0xF5EFE0, 0xB45309, 0xF0C050,
        {
            "Unlike most geckos, leopard geckos have cozy blinking eyelids!",
            "Their plump spotted tail stores spare energy reserves.",
            "They love warm belly heat on smooth flat sun-stones."
        },
        false
    },
};
