#pragma once
#include "personality_memory.h"
#include "safety_guardrails.h"
#include "../zoo/zoo_engine.h"
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// On-Chip Safe PLE Micro-LLM Runtime + Structured Personality Fallback
// Runs asynchronously on Core 0 so Core 1 animations stay at 30-60 FPS.
// =============================================================================

enum class BrainBackend : uint8_t {
    FALLBACK_RULES = 0, // Instant species + trait + memory phrase composer
    ON_CHIP_PLE    = 1, // Real INT4 Per-Layer Embedding transformer from Flash/PSRAM
};

typedef void (*BrainTokenCb)(const char* partial_text, bool finished, void* user_ctx);

void         pet_brain_init();
BrainBackend pet_brain_backend();
bool         pet_brain_has_model();
bool         pet_brain_is_busy();

// Enqueue an async generation request for Core 0. Returns false if queue full.
bool pet_brain_ask_async(const ZooPetState* pet, const PetMemory* mem,
                         uint8_t local_hour, uint8_t battery_pct,
                         const char* user_msg, bool screen_bubble,
                         BrainTokenCb cb, void* user_ctx);

// Synchronous helper used by Core 0 HTTP SSE handler (runs directly in caller or awaits Core 0)
bool pet_brain_generate_sync(const ZooPetState* pet, const PetMemory* mem,
                             uint8_t local_hour, uint8_t battery_pct,
                             const char* user_msg, bool screen_bubble,
                             char* out_reply, size_t out_len,
                             BrainTokenCb stream_cb = nullptr, void* user_ctx = nullptr);
