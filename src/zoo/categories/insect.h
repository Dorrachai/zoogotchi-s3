#pragma once
#include "../species_registry.h"

// Real-Zoo Category 2: INSECT (Permadeath)
static const SpeciesProfile kCategoryInsect[SPECIES_PER_CATEGORY] = {
    {
        6, AnimalCategory::INSECT, "monarch", "Monarch Butterfly", "Danaus plexippus",
        "Milkweed Meadow", CircadianType::DIURNAL,
        {"Egg", "Caterpillar", "Chrysalis", "Butterfly", "Elder"},
        {0, 2, 14, 34, 96}, 140,
        "Milkweed Leaf", "Wildflower Nectar", "Meadow Mist",
        0xF4F3E1, 0xD97706, 0xF59E0B,
        {
            "Monarch caterpillars eat milkweed, then spin a jade chrysalis.",
            "Their orange wings warn birds they taste bitter!",
            "Monarchs migrate thousands of kilometers using sun compasses."
        },
        false
    },
    {
        7, AnimalCategory::INSECT, "hercules_beetle", "Hercules Beetle", "Dynastes hercules",
        "Cloud Forest", CircadianType::NOCTURNAL,
        {"Egg", "Grub", "Pupa", "Horned Adult", "Elder"},
        {0, 3, 18, 46, 110}, 160,
        "Forest Moss", "Sweet Banana", "Humus Moisture",
        0xEAEFE2, 0x4D7C0F, 0x6D7C28,
        {
            "Hercules beetles can carry hundreds of times their weight!",
            "Their shell changes from olive to black in humid air.",
            "Most active at night foraging sweet fallen rainforest fruit."
        },
        false
    },
    {
        8, AnimalCategory::INSECT, "orchid_mantis", "Orchid Mantis", "Hymenopus coronatus",
        "Orchid Garden", CircadianType::DIURNAL,
        {"Ootheca", "Nymph", "Subadult", "Adult", "Elder"},
        {0, 2, 16, 42, 108}, 150,
        "Honey Drop", "Flower Fly", "Orchid Mist",
        0xFAECF4, 0xBE185D, 0xF4B6D2,
        {
            "Leg lobes look just like real pink orchid petals!",
            "Orchid mantises sway gently in the breeze to blend in.",
            "They have keen stereo vision with five eyes total."
        },
        false
    },
};
