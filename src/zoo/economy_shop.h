#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// Economy & Zoo Shop — Zoo Coins earned via mini-games, walks, & study sprints
// =============================================================================

static constexpr uint8_t SHOP_ITEM_COUNT = 10;

enum class ShopItemId : uint8_t {
    GOURMET_FEAST   = 0, // +45 hunger, +15 enrichment
    BIOME_TOY       = 1, // +40 enrichment, +8 bond
    VITAMIN_TONIC   = 2, // cures sickness & +40 health
    HABITAT_KIT     = 3, // +50 envCare & hydration
    EXPLORER_HAT    = 4, // cosmetic accessory
    LEAF_CROWN      = 5, // cosmetic accessory
    STAR_RIBBON     = 6, // cosmetic accessory
    COZY_SCARF      = 7, // cosmetic accessory
    SCHOLAR_GLASSES = 8, // study specs accessory
    GRAD_CAP        = 9, // graduation mortarboard accessory
};

struct ShopItem {
    ShopItemId  id;
    const char* name;
    const char* desc;
    uint16_t    price;
    bool        isAccessory;
};

const ShopItem& shop_get_item(uint8_t idx);
