#pragma once
#include "../species_registry.h"

// Real-Zoo Category 0: LAND (Permadeath)
static const SpeciesProfile kCategoryLand[SPECIES_PER_CATEGORY] = {
    {
        0, AnimalCategory::LAND, "red_panda", "Red Panda", "Ailurus fulgens",
        "Bamboo Forest", CircadianType::CREPUSCULAR,
        {"Newborn", "Cub", "Subadult", "Adult", "Elder"},
        {0, 4, 24, 72, 168}, 240,
        "Fresh Bamboo", "Sweet Apple", "Forest Clean",
        0xE9F2E4, 0x3A6B4C, 0xC8552E,
        {
            "Red pandas use a false thumb bone to grip bamboo stems!",
            "Their fluffy ringed tail doubles as a winter blanket.",
            "Red pandas are crepuscular: busiest at dawn and dusk."
        },
        false
    },
    {
        1, AnimalCategory::LAND, "fennec_fox", "Fennec Fox", "Vulpes zerda",
        "Sahara Dunes", CircadianType::NOCTURNAL,
        {"Newborn", "Kit", "Juvenile", "Adult", "Elder"},
        {0, 4, 20, 64, 150}, 220,
        "Desert Cricket", "Sweet Date", "Sand Clean",
        0xFAF0DC, 0xC97A3B, 0xE6B980,
        {
            "Huge ears radiate body heat and hear bugs underground!",
            "Furry paws protect fennec foxes from hot desert sand.",
            "They stay cool in burrows by day and explore at night."
        },
        false
    },
    {
        2, AnimalCategory::LAND, "capybara", "Capybara", "Hydrochoerus hydrochaeris",
        "River Marsh", CircadianType::DIURNAL,
        {"Newborn", "Pup", "Juvenile", "Adult", "Elder"},
        {0, 4, 24, 72, 168}, 240,
        "River Grass", "Watermelon", "Mud Bath",
        0xE4F1EC, 0x2B7A78, 0x8F684A,
        {
            "Capybaras are the gentle largest rodents on Earth.",
            "Webbed feet help them swim smoothly in warm rivers.",
            "Birds often perch calmly right on a capybara's back!"
        },
        false
    },
};
