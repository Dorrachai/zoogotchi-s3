#pragma once
#include "../species_registry.h"

// Real-Zoo Category 3: WATER (Permadeath)
static const SpeciesProfile kCategoryWater[SPECIES_PER_CATEGORY] = {
    {
        9, AnimalCategory::WATER, "manatee", "West Indian Manatee", "Trichechus manatus",
        "Spring Lagoon", CircadianType::CATHEMERAL,
        {"Newborn", "Calf", "Juvenile", "Adult", "Elder"},
        {0, 4, 28, 84, 192}, 280,
        "Seagrass", "Crisp Romaine", "Water Temp",
        0xDCF2F5, 0x0E7490, 0x7A8B99,
        {
            "Gentle sea cows graze seagrass up to 7 hours every day!",
            "Manatees surface every few minutes for calm, slow breaths.",
            "They love cozy warm spring water above 20 degrees Celsius."
        },
        false
    },
    {
        10, AnimalCategory::WATER, "axolotl", "Axolotl", "Ambystoma mexicanum",
        "Cool Lake", CircadianType::CREPUSCULAR,
        {"Egg", "Larva", "Juvenile", "Adult", "Elder"},
        {0, 3, 20, 60, 150}, 220,
        "Lake Pellets", "Bloodworms", "Water Quality",
        0xE2F3F0, 0x0D9488, 0xF4A6B8,
        {
            "Frilly external gills flutter like underwater ferns!",
            "Axolotls stay charmingly aquatic throughout their whole life.",
            "Clean, cool freshwater keeps an axolotl smiling wide."
        },
        false
    },
    {
        11, AnimalCategory::WATER, "octopus", "Pacific Octopus", "Enteroctopus dofleini",
        "Kelp Reef", CircadianType::NOCTURNAL,
        {"Egg", "Paralarva", "Juvenile", "Adult", "Elder"},
        {0, 3, 22, 64, 144}, 200,
        "Reef Crab", "Clam Puzzle", "Salinity",
        0xDDEBF7, 0x1D4ED8, 0xC85A4B,
        {
            "Octopuses have three hearts and clever problem-solving brains!",
            "Skin cells shift color and texture in less than a second.",
            "They love opening puzzle jars for hidden reef treats."
        },
        false
    },
};
