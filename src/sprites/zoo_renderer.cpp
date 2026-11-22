#include "zoo_renderer.h"
#include "karimono_composer.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <stdlib.h>

static uint16_t* _canvas_buf    = nullptr;
static uint8_t*  _spr_cache_buf = nullptr;
static char      _spr_cache_key[36] = {};
static bool      _spr_cache_hit     = false;

uint16_t rgb888_to_565(uint32_t hex) {
    uint8_t r = (hex >> 16) & 0xFF;
    uint8_t g = (hex >> 8)  & 0xFF;
    uint8_t b =  hex        & 0xFF;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t hsv_to_565(uint16_t h, uint8_t s_pct, uint8_t v_pct) {
    h %= 360;
    float s = s_pct / 100.0f;
    float v = v_pct / 100.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf = 0, gf = 0, bf = 0;
    if      (h < 60)  { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else              { rf = c; gf = 0; bf = x; }
    uint8_t r = (uint8_t)((rf + m) * 255.0f);
    uint8_t g = (uint8_t)((gf + m) * 255.0f);
    uint8_t b = (uint8_t)((bf + m) * 255.0f);
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t darken_565(uint16_t c, uint8_t pct) {
    uint8_t r = ((c >> 11) & 0x1F) * (100 - pct) / 100;
    uint8_t g = ((c >> 5)  & 0x3F) * (100 - pct) / 100;
    uint8_t b = ( c        & 0x1F) * (100 - pct) / 100;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

uint16_t lighten_565(uint16_t c, uint8_t pct) {
    uint8_t r = (c >> 11) & 0x1F;
    uint8_t g = (c >> 5)  & 0x3F;
    uint8_t b =  c        & 0x1F;
    r = (uint8_t)(r + (31 - r) * pct / 100);
    g = (uint8_t)(g + (63 - g) * pct / 100);
    b = (uint8_t)(b + (31 - b) * pct / 100);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void PixelCanvas64::clear(uint16_t col565) {
    bg565 = col565;
    uint32_t pair = ((uint32_t)col565 << 16) | col565;
    uint32_t* p32 = reinterpret_cast<uint32_t*>(buf128);
    constexpr int count32 = (SPRITE_CANVAS_SZ * SPRITE_CANVAS_SZ) / 2;
    for (int i = 0; i < count32; i++) p32[i] = pair;
}

void PixelCanvas64::pset(int x, int y, uint16_t col565) {
    if (x < 0 || x >= SPRITE_LOGICAL_SZ || y < 0 || y >= SPRITE_LOGICAL_SZ) return;
    int px = x * 2;
    int py = y * 2;
    buf128[py * SPRITE_CANVAS_SZ + px]           = col565;
    buf128[py * SPRITE_CANVAS_SZ + px + 1]       = col565;
    buf128[(py + 1) * SPRITE_CANVAS_SZ + px]     = col565;
    buf128[(py + 1) * SPRITE_CANVAS_SZ + px + 1] = col565;
}

void PixelCanvas64::rect(int x, int y, int w, int h, uint16_t col565) {
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) pset(xx, yy, col565);
    }
}

void PixelCanvas64::circ(int cx, int cy, int r, uint16_t col565) {
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) pset(cx + dx, cy + dy, col565);
        }
    }
}

void PixelCanvas64::ellip(int cx, int cy, int rx, int ry, uint16_t col565) {
    if (rx <= 0 || ry <= 0) return;
    for (int dy = -ry; dy <= ry; dy++) {
        for (int dx = -rx; dx <= rx; dx++) {
            if ((dx * dx * ry * ry + dy * dy * rx * rx) <= (rx * rx * ry * ry)) {
                pset(cx + dx, cy + dy, col565);
            }
        }
    }
}

void zoo_renderer_init() {
    if (_canvas_buf) return;
    size_t bytes = SPRITE_CANVAS_SZ * SPRITE_CANVAS_SZ * sizeof(uint16_t);
    _canvas_buf = reinterpret_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!_canvas_buf) {
        _canvas_buf = reinterpret_cast<uint16_t*>(malloc(bytes));
    }
}

lv_obj_t* zoo_renderer_create_canvas(lv_obj_t* parent) {
    zoo_renderer_init();
    lv_obj_t* cv = lv_canvas_create(parent);
    lv_canvas_set_buffer(cv, _canvas_buf, SPRITE_CANVAS_SZ, SPRITE_CANVAS_SZ, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(cv, SPRITE_CANVAS_SZ, SPRITE_CANVAS_SZ);
    return cv;
}

// Try loading an optional LittleFS custom sprite override (/sprites/<slug>_<stage>.bin)
// Cached in PSRAM (12,288 B = RGB565 + A8) so animations never re-seek Flash every frame.
static bool try_load_littlefs_sprite(PixelCanvas64& c, const char* slug, uint8_t stage) {
    static constexpr size_t SPR_RGB_BYTES   = 64 * 64 * 2;
    static constexpr size_t SPR_TOTAL_BYTES = SPR_RGB_BYTES + 64 * 64;

    if (!_spr_cache_buf) {
        _spr_cache_buf = reinterpret_cast<uint8_t*>(
            heap_caps_malloc(SPR_TOTAL_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!_spr_cache_buf) {
            _spr_cache_buf = reinterpret_cast<uint8_t*>(malloc(SPR_TOTAL_BYTES));
        }
        if (!_spr_cache_buf) return false;
    }

    char key[36];
    snprintf(key, sizeof(key), "%s_%u", slug, (unsigned)stage);
    if (strcmp(_spr_cache_key, key) != 0) {
        strlcpy(_spr_cache_key, key, sizeof(_spr_cache_key));
        _spr_cache_hit = false;

        char path[48];
        snprintf(path, sizeof(path), "/sprites/%s.bin", key);
        if (LittleFS.exists(path)) {
            File f = LittleFS.open(path, "r");
            if (f) {
                uint16_t w = 0, h = 0;
                if (f.read((uint8_t*)&w, 2) == 2 && f.read((uint8_t*)&h, 2) == 2 &&
                    w == 64 && h == 64 && f.read(_spr_cache_buf, SPR_TOTAL_BYTES) == SPR_TOTAL_BYTES) {
                    _spr_cache_hit = true;
                }
                f.close();
            }
        }
    }

    if (!_spr_cache_hit) return false;

    const uint8_t* rgb_p   = _spr_cache_buf;
    const uint8_t* alpha_p = _spr_cache_buf + SPR_RGB_BYTES;
    for (int y = 0; y < 64; y++) {
        int row = y * 64;
        for (int x = 0; x < 64; x++) {
            int idx = row + x;
            if (alpha_p[idx] > 128) {
                uint16_t px = (uint16_t)(rgb_p[idx * 2] | (rgb_p[idx * 2 + 1] << 8));
                c.pset(x, y, px);
            }
        }
    }
    return true;
}

static void draw_eyes(PixelCanvas64& c, int lx, int rx, int ey, PetMood mood, uint8_t frame) {
    uint16_t ink = rgb888_to_565(0x1E293B);
    uint16_t wht = rgb888_to_565(0xFFFFFF);
    if (mood == PetMood::SLEEPING) {
        c.rect(lx - 2, ey, 4, 1, ink);
        c.rect(rx - 2, ey, 4, 1, ink);
        return;
    }
    if (mood == PetMood::DIZZY) {
        c.pset(lx - 1, ey - 1, ink); c.pset(lx + 1, ey - 1, ink); c.pset(lx, ey, ink);
        c.pset(lx - 1, ey + 1, ink); c.pset(lx + 1, ey + 1, ink);
        c.pset(rx - 1, ey - 1, ink); c.pset(rx + 1, ey - 1, ink); c.pset(rx, ey, ink);
        c.pset(rx - 1, ey + 1, ink); c.pset(rx + 1, ey + 1, ink);
        return;
    }
    if (mood == PetMood::ECSTATIC) {
        // Happy upward arcs ^ ^
        c.pset(lx - 1, ey + 1, ink); c.pset(lx, ey, ink); c.pset(lx + 1, ey + 1, ink);
        c.pset(rx - 1, ey + 1, ink); c.pset(rx, ey, ink); c.pset(rx + 1, ey + 1, ink);
        return;
    }
    if ((frame % 16) == 15) {
        c.rect(lx - 1, ey, 3, 1, ink);
        c.rect(rx - 1, ey, 3, 1, ink);
        return;
    }
    c.rect(lx - 1, ey - 1, 2, 3, ink);
    c.rect(rx - 1, ey - 1, 2, 3, ink);
    c.pset(lx - 1, ey - 1, wht);
    c.pset(rx - 1, ey - 1, wht);
}

static void draw_accessory(PixelCanvas64& c, uint8_t acc, int cx, int top_y) {
    if (acc == 0) return;
    switch (acc) {
    case 4: { // Explorer Hat
        uint16_t tan  = rgb888_to_565(0xD6B37C);
        uint16_t band = rgb888_to_565(0x78350F);
        c.rect(cx - 10, top_y, 20, 2, tan);
        c.rect(cx - 6, top_y - 5, 12, 5, tan);
        c.rect(cx - 6, top_y - 1, 12, 1, band);
        break;
    }
    case 5: { // Leaf Crown
        uint16_t g1 = rgb888_to_565(0x22C55E);
        uint16_t g2 = rgb888_to_565(0x15803D);
        c.rect(cx - 8, top_y - 1, 16, 2, g2);
        c.circ(cx - 5, top_y - 3, 2, g1);
        c.circ(cx,     top_y - 4, 2, g1);
        c.circ(cx + 5, top_y - 3, 2, g1);
        break;
    }
    case 6: { // Star Ribbon
        uint16_t gold = rgb888_to_565(0xFACC15);
        uint16_t pink = rgb888_to_565(0xEC4899);
        c.rect(cx + 4, top_y - 3, 5, 3, pink);
        c.pset(cx + 6, top_y - 2, gold);
        break;
    }
    case 7: { // Cozy Scarf
        uint16_t red = rgb888_to_565(0xDC2626);
        uint16_t wht = rgb888_to_565(0xFEE2E2);
        c.rect(cx - 8, top_y + 16, 16, 3, red);
        c.rect(cx + 4, top_y + 18, 3, 5, red);
        c.pset(cx + 5, top_y + 20, wht);
        break;
    }
    case 8: { // Scholar Glasses
        uint16_t frm  = rgb888_to_565(0x1E293B);
        uint16_t lens = rgb888_to_565(0x7DD3FC);
        c.rect(cx - 9, top_y + 10, 6, 4, frm);
        c.rect(cx + 3, top_y + 10, 6, 4, frm);
        c.rect(cx - 8, top_y + 11, 4, 2, lens);
        c.rect(cx + 4, top_y + 11, 4, 2, lens);
        c.rect(cx - 3, top_y + 11, 6, 1, frm);
        break;
    }
    case 9: { // Grad Cap
        uint16_t cap  = rgb888_to_565(0x0F172A);
        uint16_t gold = rgb888_to_565(0xFACC15);
        c.rect(cx - 9, top_y - 3, 18, 2, cap);
        c.rect(cx - 5, top_y - 1, 10, 3, cap);
        c.rect(cx + 7, top_y - 1, 1, 5, gold);
        c.pset(cx, top_y - 3, gold);
        break;
    }
    default: break;
    }
}

static void draw_mood_fx(PixelCanvas64& c, const ZooPetState* s, PetMood mood, uint32_t now_ms, uint8_t frame) {
    uint16_t sky = rgb888_to_565(0x38BDF8);
    uint16_t rse = rgb888_to_565(0xFB7185);
    uint16_t gld = rgb888_to_565(0xFACC15);

    if (mood == PetMood::SLEEPING) {
        int off = (frame % 4);
        c.rect(45, 14 - off, 4, 1, sky);
        c.pset(47, 15 - off, sky);
        c.pset(46, 16 - off, sky);
        c.rect(45, 17 - off, 4, 1, sky);
        c.rect(51, 10 - off, 3, 1, sky);
        c.pset(52, 11 - off, sky);
        c.rect(51, 12 - off, 3, 1, sky);
    } else if (mood == PetMood::SICK) {
        c.circ(49, 20 + (frame & 1), 2, sky);
        c.pset(49, 17 + (frame & 1), sky);
    } else if (mood == PetMood::DIZZY) {
        int dx = (frame & 1) ? 2 : -2;
        c.pset(24 + dx, 12, gld);
        c.pset(32 - dx, 10, gld);
        c.pset(40 + dx, 12, gld);
    }

    if (s->lastPetHeartMs > 0 && (now_ms - s->lastPetHeartMs) < 2500) {
        int hy = 15 - (int)((now_ms - s->lastPetHeartMs) / 280);
        c.pset(46, hy, rse); c.pset(48, hy, rse);
        c.rect(45, hy + 1, 5, 2, rse);
        c.pset(47, hy + 3, rse);
    }
}

static void draw_egg(PixelCanvas64& c, uint16_t col, uint16_t acc, int bob) {
    uint16_t shadow = darken_565(col, 25);
    c.ellip(32, 48, 12, 3, rgb888_to_565(0xCBD5E1));
    c.ellip(32, 36 + bob, 10, 13, col);
    c.ellip(32, 39 + bob, 9, 8, shadow);
    c.ellip(32, 36 + bob, 9, 11, col);
    c.circ(28, 33 + bob, 2, acc);
    c.circ(36, 38 + bob, 2, acc);
    c.circ(31, 42 + bob, 1, acc);
}

static void draw_real_animal(PixelCanvas64& c, const ZooPetState* s, const SpeciesProfile& sp,
                             PetMood mood, uint8_t frame, int bob) {
    uint16_t body = rgb888_to_565(sp.bodyColorHex);
    uint16_t acc  = rgb888_to_565(sp.biomeAccentHex);
    uint16_t dark = darken_565(body, 28);
    uint16_t lite = lighten_565(body, 32);
    uint16_t ink  = rgb888_to_565(0x1E293B);
    uint16_t wht  = rgb888_to_565(0xFFFFFF);

    // Ground shadow
    c.ellip(32, 52, 15, 3, darken_565(c.bg565, 16));

    // Special stage 0 Egg for egg-laying species (Sky 3..5, Insect 6..8, Axolotl/Octopus 10..11, Reptile 12..14)
    if (s->stage == 0 && (sp.id >= 3 && sp.id != 9 && !sp.isKarimono)) {
        draw_egg(c, lite, acc, bob);
        return;
    }

    int y = 36 + bob;
    switch (sp.id) {
    case 0: { // Red Panda
        // Ringed fluffy tail
        c.ellip(45, y + 6, 8, 4, body);
        c.rect(42, y + 3, 2, 6, dark);
        c.rect(47, y + 3, 2, 6, dark);
        // Body & white belly
        c.ellip(30, y + 5, 11, 9, dark);
        c.ellip(30, y - 5, 10, 8, body);
        // Ears & cheeks
        c.circ(22, y - 12, 3, body); c.circ(38, y - 12, 3, body);
        c.circ(22, y - 12, 1, wht);  c.circ(38, y - 12, 1, wht);
        c.ellip(25, y - 3, 3, 2, wht); c.ellip(35, y - 3, 3, 2, wht);
        c.rect(28, y - 4, 4, 3, dark); // muzzle
        c.pset(30, y - 3, ink);
        draw_eyes(c, 26, 34, y - 6, mood, frame);
        break;
    }
    case 1: { // Fennec Fox
        // Huge ears
        c.ellip(22, y - 14, 4, 9, body); c.ellip(22, y - 13, 2, 6, rgb888_to_565(0xFBCFE8));
        c.ellip(42, y - 14, 4, 9, body); c.ellip(42, y - 13, 2, 6, rgb888_to_565(0xFBCFE8));
        // Tail
        c.ellip(44, y + 7, 7, 3, body); c.pset(49, y + 7, dark);
        // Body & head
        c.ellip(32, y + 6, 9, 7, lite);
        c.ellip(32, y - 4, 9, 7, body);
        c.pset(32, y - 1, ink);
        draw_eyes(c, 28, 36, y - 4, mood, frame);
        break;
    }
    case 2: { // Capybara
        c.ellip(32, y + 4, 14, 10, body);
        c.ellip(22, y - 4, 9, 7, body);
        c.circ(25, y - 11, 2, dark);
        c.rect(14, y - 3, 4, 4, dark); // blunt nose
        c.rect(24, y + 11, 3, 4, dark);
        c.rect(36, y + 11, 3, 4, dark);
        // Little orange yuzu on head when ecstatic!
        if (mood == PetMood::ECSTATIC) c.circ(22, y - 12, 3, rgb888_to_565(0xF97316));
        draw_eyes(c, 19, 25, y - 5, mood, frame);
        break;
    }
    case 3: { // Barn Owl
        c.ellip(32, y + 2, 11, 13, lite);
        c.ellip(32, y + 4, 8, 9, wht);
        c.ellip(20, y + 3, 3, 9, body);
        c.ellip(44, y + 3, 3, 9, body);
        // Heart-shaped facial disc
        c.circ(28, y - 6, 5, wht);
        c.circ(36, y - 6, 5, wht);
        c.pset(32, y - 4, rgb888_to_565(0xF59E0B));
        c.pset(32, y - 3, rgb888_to_565(0xF59E0B));
        draw_eyes(c, 28, 36, y - 7, mood, frame);
        break;
    }
    case 4: { // Scarlet Macaw
        c.ellip(32, y + 1, 10, 12, body);
        c.ellip(41, y + 3, 4, 9, rgb888_to_565(0x2563EB)); // blue wing
        c.rect(38, y - 1, 5, 3, rgb888_to_565(0xFACC15)); // yellow wing bar
        c.rect(28, y + 12, 3, 7, rgb888_to_565(0x2563EB)); // long tail
        c.circ(25, y - 6, 4, wht); // white face patch
        c.ellip(19, y - 5, 4, 3, rgb888_to_565(0xFEF3C7)); // curved beak
        draw_eyes(c, 25, 25, y - 7, mood, frame);
        break;
    }
    case 5: { // Ruby Hummingbird
        int flap = (frame & 1) ? -4 : 3;
        c.ellip(38, y - 4 + flap, 7, 4, lighten_565(body, 25)); // fluttering wing
        c.ellip(30, y + 1, 8, 9, body);
        c.ellip(27, y - 2, 4, 4, rgb888_to_565(0xE11D48)); // ruby throat
        c.rect(14, y - 6, 9, 1, ink); // needle beak
        draw_eyes(c, 26, 26, y - 6, mood, frame);
        break;
    }
    case 6: { // Monarch Butterfly (stages: 1=Caterpillar, 2=Chrysalis, 3+=Butterfly)
        if (s->stage == 1) {
            for (int i = 0; i < 4; i++) {
                c.circ(20 + i * 7, y + 6 - (i & 1), 4, (i & 1) ? rgb888_to_565(0xFACC15) : ink);
            }
            draw_eyes(c, 19, 22, y + 5, mood, frame);
        } else if (s->stage == 2) {
            c.ellip(32, y, 7, 12, rgb888_to_565(0x34D399)); // jade chrysalis
            c.rect(26, y - 4, 12, 1, rgb888_to_565(0xFACC15)); // gold dots band
        } else {
            int wing_w = (frame & 1) ? 10 : 13;
            c.ellip(32 - wing_w, y - 3, wing_w, 9, body);
            c.ellip(32 + wing_w, y - 3, wing_w, 9, body);
            c.ellip(32 - wing_w + 2, y + 6, wing_w - 3, 6, body);
            c.ellip(32 + wing_w - 2, y + 6, wing_w - 3, 6, body);
            c.ellip(32, y + 1, 2, 11, ink);
            c.pset(30, y - 11, ink); c.pset(34, y - 11, ink);
            draw_eyes(c, 31, 33, y - 6, mood, frame);
        }
        break;
    }
    case 7: { // Hercules Beetle
        if (s->stage <= 2) {
            c.ellip(32, y + 4, 10, 7, rgb888_to_565(0xFEF3C7));
            c.circ(23, y + 2, 4, rgb888_to_565(0x92400E));
            draw_eyes(c, 21, 24, y + 2, mood, frame);
        } else {
            c.ellip(36, y + 3, 12, 8, body); // olive carapace
            c.circ(24, y + 3, 7, ink);       // black thorax
            c.rect(11, y - 2, 11, 2, ink);   // upper horn
            c.rect(14, y + 3, 8, 2, ink);    // lower horn
            c.pset(12, y - 1, ink);
            draw_eyes(c, 22, 22, y + 1, mood, frame);
        }
        break;
    }
    case 8: { // Orchid Mantis
        uint16_t pink = rgb888_to_565(0xF472B6);
        c.ellip(32, y + 6, 7, 8, lite);
        c.ellip(22, y + 6, 5, 4, pink); // petal legs
        c.ellip(42, y + 6, 5, 4, pink);
        c.rect(31, y - 6, 3, 9, body); // slender thorax
        c.ellip(32, y - 9, 6, 4, lite); // triangle head
        c.pset(28, y - 14, pink); c.pset(36, y - 14, pink);
        draw_eyes(c, 28, 36, y - 9, mood, frame);
        break;
    }
    case 9: { // West Indian Manatee (featured Water species!)
        uint16_t sea  = rgb888_to_565(0x94A3B8);
        uint16_t seal = rgb888_to_565(0xCBD5E1);
        int swim = (frame & 1) ? 1 : -1;
        // Paddle tail
        c.ellip(48, y + 4 + swim, 6, 5, sea);
        // Plump body & snout
        c.ellip(31, y + 3, 15, 10, sea);
        c.ellip(29, y + 6, 11, 5, seal);
        c.ellip(18, y + 2, 7, 6, sea);   // rounded muzzle
        c.ellip(16, y + 4, 4, 3, seal);
        // Flippers
        c.ellip(25, y + 10, 4, 3, dark);
        c.ellip(37, y + 10, 4, 3, dark);
        // Surfacing water bubbles
        c.circ(14, y - 12 - (frame % 3) * 2, 2, rgb888_to_565(0x38BDF8));
        c.circ(19, y - 16 - (frame % 2) * 2, 1, rgb888_to_565(0x7DD3FC));
        draw_eyes(c, 19, 23, y, mood, frame);
        break;
    }
    case 10: { // Axolotl
        uint16_t gill = rgb888_to_565(0xEC4899);
        c.ellip(44, y + 5, 8, 4, lite); // swim tail
        c.ellip(31, y + 5, 10, 7, body);
        c.ellip(25, y - 3, 10, 7, body);
        // 3 frilly gills on each side
        for (int g = -1; g <= 1; g++) {
            c.rect(12, y - 4 + g * 3, 4, 2, gill);
            c.rect(35, y - 4 + g * 3, 4, 2, gill);
        }
        c.rect(23, y, 5, 1, ink); // wide smile
        draw_eyes(c, 22, 29, y - 4, mood, frame);
        break;
    }
    case 11: { // Giant Pacific Octopus
        c.ellip(32, y - 4, 11, 10, body); // bulbous mantle
        // Tentacles waving
        for (int t = 0; t < 5; t++) {
            int tx = 18 + t * 7;
            int ty = y + 7 + ((t + frame) & 1) * 2;
            c.ellip(tx, ty, 3, 6, dark);
            c.pset(tx, ty + 2, lite);
        }
        draw_eyes(c, 27, 36, y - 2, mood, frame);
        break;
    }
    case 12: { // Galapagos Tortoise
        uint16_t shell = rgb888_to_565(0x57534E);
        c.ellip(35, y + 1, 13, 10, shell);
        c.circ(32, y - 1, 3, acc); c.circ(39, y - 1, 3, acc);
        c.ellip(18, y + 1, 6, 4, body); // long neck + head
        c.rect(25, y + 9, 4, 5, dark);
        c.rect(40, y + 9, 4, 5, dark);
        draw_eyes(c, 16, 19, y, mood, frame);
        break;
    }
    case 13: { // Veiled Chameleon — dynamic mood hue!
        uint16_t dyn  = hsv_to_565(zoo_get_chameleon_hue(s), 68, 78);
        uint16_t dlit = lighten_565(dyn, 25);
        // Curled spiral tail
        c.circ(45, y + 5, 5, dyn);
        c.circ(45, y + 5, 2, c.bg565);
        c.ellip(31, y + 2, 11, 8, dyn);
        c.rect(26, y - 2, 3, 7, dlit);
        c.rect(33, y - 2, 3, 7, dlit);
        // High casque crest
        c.ellip(20, y - 4, 6, 7, dyn);
        c.circ(19, y - 11, 3, dlit);
        draw_eyes(c, 18, 22, y - 4, mood, frame);
        break;
    }
    case 14: { // Leopard Gecko
        c.ellip(45, y + 6, 7, 4, body); // plump tail
        c.ellip(31, y + 5, 11, 6, body);
        c.ellip(20, y + 1, 7, 5, body);
        c.pset(28, y + 3, ink); c.pset(34, y + 6, ink); c.pset(44, y + 6, ink); // spots
        c.rect(23, y + 10, 3, 4, lite);
        c.rect(36, y + 10, 3, 4, lite);
        draw_eyes(c, 18, 22, y, mood, frame);
        break;
    }
    default: break;
    }
}

void zoo_renderer_draw(lv_obj_t* canvas_obj, const ZooPetState* state, uint32_t now_ms, uint8_t anim_frame) {
    if (!canvas_obj || !_canvas_buf || !state) return;

    const SpeciesProfile& sp = species_get(state->speciesId);
    PixelCanvas64 c{_canvas_buf, rgb888_to_565(sp.biomeBgHex)};
    c.clear(c.bg565);

    // Soft biome floor accent strip
    c.rect(0, 52, 64, 12, darken_565(c.bg565, 8));

    PetMood mood = zoo_compute_mood(state, now_ms);
    int bob = (mood == PetMood::SLEEPING) ? 0 : ((anim_frame & 1) ? -1 : 0);

    if (state->isDead) {
        // Gentle floral memorial sprite
        uint16_t stone = rgb888_to_565(0x94A3B8);
        uint16_t moss  = rgb888_to_565(0x22C55E);
        c.ellip(32, 39, 10, 13, stone);
        c.rect(22, 46, 20, 6, darken_565(stone, 20));
        c.circ(26, 48, 2, moss);
        c.circ(37, 47, 2, rgb888_to_565(0xF472B6));
        lv_obj_invalidate(canvas_obj);
        return;
    }

    bool can_use_static = (!sp.isKarimono || state->genome.generation <= 1);
    bool loaded_sprite = false;
    if (can_use_static) {
        loaded_sprite = try_load_littlefs_sprite(c, sp.slug, state->stage);
        if (!loaded_sprite && !sp.isKarimono && state->stage >= 2) {
            loaded_sprite = try_load_littlefs_sprite(c, sp.slug, 3);
        }
    }
    if (loaded_sprite) {
        draw_accessory(c, state->equippedAccessory, 32, 18 + bob);
        draw_mood_fx(c, state, mood, now_ms, anim_frame);
        lv_obj_invalidate(canvas_obj);
        return;
    }

    if (sp.isKarimono) {
        karimono_composer_draw(c, state, mood, anim_frame, bob);
    } else {
        draw_real_animal(c, state, sp, mood, anim_frame, bob);
    }

    draw_accessory(c, state->equippedAccessory, 32, 18 + bob);
    draw_mood_fx(c, state, mood, now_ms, anim_frame);
    lv_obj_invalidate(canvas_obj);
}
