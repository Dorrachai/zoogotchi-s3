#pragma once
#include "../species_registry.h"

// Real-Zoo Category 1: SKY (Permadeath)
static const SpeciesProfile kCategorySky[SPECIES_PER_CATEGORY] = {
    {
        3, AnimalCategory::SKY, "barn_owl", "Barn Owl", "Tyto alba",
        "Moonlit Meadow", CircadianType::NOCTURNAL,
        {"Egg", "Owlet", "Fledgling", "Adult", "Elder"},
        {0, 2, 22, 68, 160}, 230,
        "Meadow Snack", "Berry Treat", "Nest Clean",
        0xDCE4F0, 0x3D4C73, 0xEAE0CE,
        {
            "Heart-shaped facial feathers funnel sound right to the ears.",
            "Serrated wing edges make barn owl flight nearly silent!",
            "Barn owls hunt by starlight and roost quietly by day."
        },
        false
    },
    {
        4, AnimalCategory::SKY, "scarlet_macaw", "Scarlet Macaw", "Ara macao",
        "Amazon Canopy", CircadianType::DIURNAL,
        {"Egg", "Chick", "Fledgling", "Adult", "Elder"},
        {0, 3, 28, 84, 192}, 300,
        "Canopy Nuts", "Mango Slice", "Perch Care",
        0xE5F4E6, 0x2F855A, 0xD93829,
        {
            "Macaws crack hard palm nuts with ease using curved beaks.",
            "Scarlet macaws visit river clay licks for natural minerals.",
            "Strong bonds make them playful, chatty flock friends!"
        },
        false
    },
    {
        5, AnimalCategory::SKY, "hummingbird", "Ruby Hummingbird", "Archilochus colubris",
        "Flower Glade", CircadianType::DIURNAL,
        {"Egg", "Chick", "Fledgling", "Adult", "Elder"},
        {0, 2, 16, 48, 120}, 180,
        "Floral Nectar", "Orchid Dew", "Blossom Care",
        0xF8EAF2, 0xB83280, 0x2E8B57,
        {
            "Hummingbirds beat their wings over 50 times per second!",
            "They are the only birds able to fly backward smoothly.",
            "At night they enter cozy torpor to save tiny energy reserves."
        },
        false
    },
};
