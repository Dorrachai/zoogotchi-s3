#pragma once
#include "../zoo/zoo_engine.h"
#include <lvgl.h>
#include <stdint.h>

// =============================================================================
// ZooGotchi S3 — 128x128 (64x64 @ 2x pixel-art) PSRAM Sprite Canvas Renderer
// Supports LittleFS /sprites/<slug>_<stage>.bin overrides + procedural pixel art.
// =============================================================================

static constexpr int SPRITE_LOGICAL_SZ = 64;
static constexpr int SPRITE_CANVAS_SZ  = 128;

// Pixel buffer helper passed to both zoo_renderer & karimono_composer
struct PixelCanvas64 {
    uint16_t* buf128; // 128x128 RGB565 (LV_COLOR_FORMAT_RGB565)
    uint16_t  bg565;

    void clear(uint16_t col565);
    void pset(int x, int y, uint16_t col565);
    void rect(int x, int y, int w, int h, uint16_t col565);
    void circ(int cx, int cy, int r, uint16_t col565);
    void ellip(int cx, int cy, int rx, int ry, uint16_t col565);
};

uint16_t rgb888_to_565(uint32_t hex);
uint16_t hsv_to_565(uint16_t h, uint8_t s_pct, uint8_t v_pct);
uint16_t darken_565(uint16_t c, uint8_t pct);
uint16_t lighten_565(uint16_t c, uint8_t pct);

void zoo_renderer_init();
lv_obj_t* zoo_renderer_create_canvas(lv_obj_t* parent);
void zoo_renderer_draw(lv_obj_t* canvas_obj, const ZooPetState* state, uint32_t now_ms, uint8_t anim_frame);
