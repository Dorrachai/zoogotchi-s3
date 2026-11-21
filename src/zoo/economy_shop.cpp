#include "economy_shop.h"

static const ShopItem kShopItems[SHOP_ITEM_COUNT] = {
    {ShopItemId::GOURMET_FEAST,   "Gourmet Feast",   "Full meal + joy",      18, false},
    {ShopItemId::BIOME_TOY,       "Biome Toy",       "Enrich + bond",        22, false},
    {ShopItemId::VITAMIN_TONIC,   "Vet Tonic",       "Heal & boost",         20, false},
    {ShopItemId::HABITAT_KIT,     "Habitat Kit",     "Water/temp/clean",     15, false},
    {ShopItemId::EXPLORER_HAT,    "Explorer Hat",    "Safari cap accessory", 45, true},
    {ShopItemId::LEAF_CROWN,      "Leaf Crown",      "Botanical crown",      50, true},
    {ShopItemId::STAR_RIBBON,     "Star Ribbon",     "Shining bow",          55, true},
    {ShopItemId::COZY_SCARF,      "Cozy Scarf",      "Warm neck scarf",      60, true},
    {ShopItemId::SCHOLAR_GLASSES, "Scholar Glasses", "Study specs",          35, true},
    {ShopItemId::GRAD_CAP,        "Grad Cap",        "Mastery mortarboard",  55, true},
};

const ShopItem& shop_get_item(uint8_t idx) {
    if (idx >= SHOP_ITEM_COUNT) return kShopItems[0];
    return kShopItems[idx];
}
