#pragma once
#include "zoo_renderer.h"
#include "../zoo/zoo_engine.h"

// =============================================================================
// Karimono Composable Pixel-Art Genetics Renderer
// Combines bodyGene, earHornGene, eyeGene, tailWingGene, patternGene,
// primaryHue, accentHue & mutationMask into unique cross-bred creatures!
// =============================================================================

void karimono_composer_draw(PixelCanvas64& c, const ZooPetState* state,
                            PetMood mood, uint8_t frame, int bob);
