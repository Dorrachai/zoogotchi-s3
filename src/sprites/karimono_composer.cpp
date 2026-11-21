#include "karimono_composer.h"

void karimono_composer_draw(PixelCanvas64& c, const ZooPetState* state,
                            PetMood mood, uint8_t frame, int bob) {
    const KarimonoGenome& g = state->genome;
    uint16_t prim = hsv_to_565(g.primaryHue, 72, 90);
    uint16_t acc  = hsv_to_565(g.accentHue,  78, 86);
    uint16_t dark = darken_565(prim, 28);
    uint16_t lite = lighten_565(prim, 35);
    uint16_t ink  = rgb888_to_565(0x1E293B);
    uint16_t wht  = rgb888_to_565(0xFFFFFF);

    c.ellip(32, 52, 14, 3, darken_565(c.bg565, 16));

    // Egg stage (stage 0)
    if (state->stage == 0) {
        c.ellip(32, 36 + bob, 10, 13, prim);
        c.rect(24, 34 + bob, 16, 3, acc);
        c.circ(32, 30 + bob, 2, lite);
        return;
    }

    int y = 35 + bob;
    int scale = (state->stage == 1) ? 9 : 13;

    // 1. Rare Star Glow mutation aura
    if (g.mutationMask & KARIMONO_MUT_STAR_GLOW) {
        uint16_t aura = rgb888_to_565(0xFEF08A);
        int sp = (frame & 1) ? 1 : 0;
        c.pset(15, y - 12 - sp, aura);
        c.pset(49, y - 10 + sp, aura);
        c.pset(16, y + 10 + sp, aura);
        c.pset(48, y + 9 - sp, aura);
    }

    // 2. Tail / Wings (behind body)
    int flap = (frame & 1) ? -2 : 2;
    uint16_t wingCol = (g.mutationMask & KARIMONO_MUT_CRYSTAL_WING) ? rgb888_to_565(0x7DD3FC) : acc;
    switch (g.tailWingGene % 6) {
    case 1: c.circ(45, y + 6, 5, acc); break; // Fluffy tail
    case 2: // Angel wings
        c.ellip(16, y + flap, 5, 7, wht);
        c.ellip(48, y + flap, 5, 7, wht);
        break;
    case 3: // Bat wings
        c.ellip(16, y - 1 + flap, 6, 5, wingCol);
        c.ellip(48, y - 1 + flap, 6, 5, wingCol);
        break;
    case 4: // Star tail
        c.rect(42, y + 5, 6, 2, wingCol);
        c.circ(49, y + 5, 3, rgb888_to_565(0xFACC15));
        break;
    case 5: // Leaf tail
        c.ellip(46, y + 4, 6, 4, rgb888_to_565(0x22C55E));
        break;
    default: break;
    }

    // 3. Body shape (0..5)
    switch (g.bodyGene % 6) {
    case 0: c.circ(32, y + 2, scale, prim); break;                               // Round mochi
    case 1: c.ellip(32, y + 3, scale - 1, scale + 2, prim); break;               // Teardrop
    case 2: c.rect(32 - scale, y - scale + 3, scale * 2, scale * 2 - 2, prim); break; // Square mochi
    case 3: // Star-blob
        c.circ(32, y + 2, scale - 1, prim);
        c.ellip(32, y + 2, scale + 3, 6, prim);
        break;
    case 4: c.ellip(32, y + 2, scale + 3, scale - 2, prim); break;               // Long-cloud
    default: c.ellip(32, y + 3, scale, scale, prim); break;                      // Horned puff
    }

    // 4. Pattern gene (0..3) + Prism mutation
    uint16_t patCol = (g.mutationMask & KARIMONO_MUT_PRISM_PAT) ? rgb888_to_565(0xF472B6) : acc;
    switch (g.patternGene % 4) {
    case 1: c.ellip(32, y + 6, 7, 5, lite); break;        // Belly patch
    case 2: c.rect(23, y + 4, 18, 2, patCol); c.rect(25, y + 8, 14, 2, patCol); break; // Stripes
    case 3: c.pset(24, y + 2, patCol); c.pset(40, y + 2, patCol); c.pset(32, y + 8, patCol); break; // Freckles
    default: break;
    }

    // Little feet
    c.rect(25, y + scale, 4, 3, dark);
    c.rect(35, y + scale, 4, 3, dark);

    // 5. Ears / Horns (0..5)
    int top_y = y - scale + 1;
    switch (g.earHornGene % 6) {
    case 1: // Cat ears
        c.rect(22, top_y - 4, 4, 5, prim); c.rect(38, top_y - 4, 4, 5, prim);
        c.pset(23, top_y - 2, acc); c.pset(39, top_y - 2, acc);
        break;
    case 2: // Bunny ears
        c.ellip(25, top_y - 6, 2, 6, prim); c.ellip(39, top_y - 6, 2, 6, prim);
        break;
    case 3: // Twin horns
        c.rect(24, top_y - 5, 2, 6, acc); c.rect(38, top_y - 5, 2, 6, acc);
        c.pset(23, top_y - 5, acc); c.pset(39, top_y - 5, acc);
        break;
    case 4: // Halo antenna
        c.rect(31, top_y - 5, 2, 5, ink);
        c.circ(32, top_y - 7, 3, rgb888_to_565(0xFACC15));
        break;
    case 5: // Leaf sprout
        c.rect(31, top_y - 5, 2, 5, rgb888_to_565(0x16A34A));
        c.ellip(28, top_y - 6, 3, 2, rgb888_to_565(0x4ADE80));
        c.ellip(36, top_y - 6, 3, 2, rgb888_to_565(0x4ADE80));
        break;
    default: break;
    }

    // 6. Eyes (0..5)
    int lx = 27, rx = 37, ey = y - 1;
    if (mood == PetMood::SLEEPING) {
        c.rect(lx - 2, ey, 4, 1, ink);
        c.rect(rx - 2, ey, 4, 1, ink);
    } else if (mood == PetMood::DIZZY) {
        c.pset(lx - 1, ey - 1, ink); c.pset(lx + 1, ey + 1, ink);
        c.pset(rx - 1, ey - 1, ink); c.pset(rx + 1, ey + 1, ink);
    } else if ((frame % 16) == 15) {
        c.rect(lx - 1, ey, 3, 1, ink);
        c.rect(rx - 1, ey, 3, 1, ink);
    } else {
        switch (g.eyeGene % 6) {
        case 2: // Starry
            c.rect(lx - 1, ey - 1, 3, 3, ink); c.pset(lx, ey, rgb888_to_565(0xFACC15));
            c.rect(rx - 1, ey - 1, 3, 3, ink); c.pset(rx, ey, rgb888_to_565(0xFACC15));
            break;
        case 3: // Sleepy calm
            c.rect(lx - 2, ey, 4, 2, ink); c.rect(rx - 2, ey, 4, 2, ink);
            break;
        case 4: // Heart eyes
            c.rect(lx - 1, ey - 1, 3, 2, rgb888_to_565(0xEC4899));
            c.rect(rx - 1, ey - 1, 3, 2, rgb888_to_565(0xEC4899));
            break;
        case 5: // Pixel visor
            c.rect(23, ey - 2, 18, 4, ink);
            c.rect(25, ey - 1, 4, 2, rgb888_to_565(0x38BDF8));
            c.rect(35, ey - 1, 4, 2, rgb888_to_565(0x38BDF8));
            break;
        default: // Sparkle / Dot
            c.rect(lx - 1, ey - 1, 3, 3, ink); c.pset(lx - 1, ey - 1, wht);
            c.rect(rx - 1, ey - 1, 3, 3, ink); c.pset(rx - 1, ey - 1, wht);
            break;
        }
    }

    // Little happy mouth
    c.rect(31, y + 3, 3, 1, ink);
}
