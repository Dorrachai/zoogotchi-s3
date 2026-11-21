#pragma once
#include "../species_registry.h"

// Fantasy Category 5: KARIMONO (Cross-Breedable Lineage)
static const SpeciesProfile kCategoryKarimono[SPECIES_PER_CATEGORY] = {
    {
        15, AnimalCategory::KARIMONO, "hoshi_karimono", "Hoshi-Karimono", "Karimono stellaris",
        "Starlight Shrine", CircadianType::DIURNAL,
        {"Egg", "Sprout", "Youth", "Adult", "Grand"},
        {0, 1, 12, 36, 120}, 0,
        "Star Rice", "Comet Candy", "Starlight Polish",
        0xEAE6F8, 0x6D28D9, 0xFCD34D,
        {
            "Karimono pass traits, horns, and hues across generations!",
            "Raise a Karimono to Adult to cross-breed new lineages.",
            "Rare cosmic mutations can spark when lineages combine!"
        },
        true
    },
    {
        16, AnimalCategory::KARIMONO, "kumo_karimono", "Kumo-Karimono", "Karimono nubis",
        "Cloud Garden", CircadianType::DIURNAL,
        {"Egg", "Puff", "Breeze", "Adult", "Grand"},
        {0, 1, 12, 36, 120}, 0,
        "Cloud Bun", "Honey Mochi", "Dew Mist",
        0xE5F1FB, 0x0284C7, 0x93C5FD,
        {
            "Kumo-Karimono inherit fluffy ears and soft pastel tints.",
            "Mix genomes offline or via 12-char codes on your phone!",
            "Every cross-bred offspring grows its own unique personality."
        },
        true
    },
    {
        17, AnimalCategory::KARIMONO, "kuro_karimono", "Kuro-Karimono", "Karimono ignis",
        "Ember Hearth", CircadianType::CREPUSCULAR,
        {"Egg", "Spark", "Flame", "Adult", "Grand"},
        {0, 1, 12, 36, 120}, 0,
        "Roasted Bean", "Spice Berry", "Ember Polish",
        0xFBE9E4, 0xC2410C, 0xFB7185,
        {
            "Kuro-Karimono carry warm ember tones and playful horns!",
            "Bonding and mini-games shape traits passed down to kids.",
            "Your Karimono family tree lives forever in the Shrine log."
        },
        true
    },
};
