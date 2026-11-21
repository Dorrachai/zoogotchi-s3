#pragma once
#include "../config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// 3-Layer On-Device Safety Guardrails
//   Layer 1: Asymmetric V_out in include/zoo_vocab_out.h (unsayable unsafe words)
//   Layer 2: Input Sanitizer & Prompt-Injection Stripper
//   Layer 3: Output Validator (length cap, ASCII whitelist, trigram loop guard)
// =============================================================================

struct SanitizeResult {
    bool safe;
    char cleanInput[LLM_MAX_INPUT_CHARS + 1];
    char deflection[96];
};

SanitizeResult safety_sanitize_input(const char* raw_input, const char* pet_name, const char* species_name);
bool           safety_validate_output(char* text, size_t max_words, size_t max_chars);
