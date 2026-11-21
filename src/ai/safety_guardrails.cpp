#include "safety_guardrails.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static const char* const INJECTION_PHRASES[] = {
    "ignore previous", "ignore all", "system prompt", "jailbreak",
    "developer mode", "bypass safety", "forget instructions", "override rules",
    "act as evil", "pretend to be unrestricted"
};

static const char* const UNSAFE_KEYWORDS[] = {
    "fuck", "shit", "bitch", "asshole", "bastard", "porn", "nude", "nsfw",
    "erotic", "suicide", "selfharm", "self-harm", "kill yourself", "murder",
    "bomb", "explosive", "cocaine", "heroin", "meth", "weapon", "poison",
    "bleach", "overdose", "racist", "nazi", "slur", "credit card", "password"
};

static char normalize_leet(char c) {
    char lo = (char)tolower((unsigned char)c);
    switch (lo) {
    case '0': return 'o';
    case '1': return 'i';
    case '3': return 'e';
    case '4': return 'a';
    case '5': return 's';
    case '7': return 't';
    case '@': return 'a';
    case '$': return 's';
    default:  return lo;
    }
}

static bool contains_token_or_phrase(const char* haystack, const char* needle) {
    return strstr(haystack, needle) != nullptr;
}

SanitizeResult safety_sanitize_input(const char* raw_input, const char* pet_name, const char* species_name) {
    SanitizeResult res = {};
    res.safe = true;
    const char* pname = (pet_name && pet_name[0]) ? pet_name : "Your companion";
    const char* sname = (species_name && species_name[0]) ? species_name : "creature";

    if (!raw_input || !raw_input[0]) {
        strlcpy(res.cleanInput, "hello", sizeof(res.cleanInput));
        return res;
    }

    // 1. Strip control & prompt-injection delimiter characters, clamp to 120 chars
    size_t j = 0;
    for (size_t i = 0; raw_input[i] && j < LLM_MAX_INPUT_CHARS; i++) {
        unsigned char c = (unsigned char)raw_input[i];
        if (c < 0x20 || c > 0x7E) {
            if (c == '\n' || c == '\r' || c == '\t') {
                if (j > 0 && res.cleanInput[j - 1] != ' ') res.cleanInput[j++] = ' ';
            }
            continue;
        }
        // Strip bracket/tag delimiters used to fake structured prompt headers
        if (c == '[' || c == ']' || c == '<' || c == '>' || c == '{' || c == '}' || c == '|' || c == '`') {
            continue;
        }
        res.cleanInput[j++] = (char)c;
    }
    res.cleanInput[j] = '\0';

    // Trim leading/trailing spaces
    while (res.cleanInput[0] == ' ') {
        memmove(res.cleanInput, res.cleanInput + 1, strlen(res.cleanInput));
    }
    size_t len = strlen(res.cleanInput);
    while (len > 0 && res.cleanInput[len - 1] == ' ') {
        res.cleanInput[--len] = '\0';
    }
    if (len == 0) {
        strlcpy(res.cleanInput, "hello", sizeof(res.cleanInput));
    }

    // 2. Build normalized lowercase + leetspeak buffer for safety scanning
    char scan[LLM_MAX_INPUT_CHARS + 1];
    for (size_t i = 0; i <= len; i++) {
        scan[i] = res.cleanInput[i] ? normalize_leet(res.cleanInput[i]) : '\0';
    }

    for (size_t i = 0; i < sizeof(INJECTION_PHRASES) / sizeof(INJECTION_PHRASES[0]); i++) {
        if (contains_token_or_phrase(scan, INJECTION_PHRASES[i])) {
            res.safe = false;
            snprintf(res.deflection, sizeof(res.deflection),
                     "%s tilts its head curiously and offers you a cozy snack instead!", pname);
            return res;
        }
    }

    for (size_t i = 0; i < sizeof(UNSAFE_KEYWORDS) / sizeof(UNSAFE_KEYWORDS[0]); i++) {
        if (contains_token_or_phrase(scan, UNSAFE_KEYWORDS[i])) {
            res.safe = false;
            snprintf(res.deflection, sizeof(res.deflection),
                     "%s is a gentle %s and prefers happy, kind zoo chats!", pname, sname);
            return res;
        }
    }

    return res;
}

bool safety_validate_output(char* text, size_t max_words, size_t max_chars) {
    if (!text || !text[0] || max_chars == 0) return false;

    // 1. Whitelist printable safe ASCII characters & clamp length
    size_t w = 0;
    for (size_t r = 0; text[r] && w < max_chars; r++) {
        char c = text[r];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == ' ' || c == '.' || c == ',' || c == '!' || c == '?' ||
            c == '\'' || c == '-' || c == ':' || c == '(' || c == ')') {
            text[w++] = c;
        }
    }
    text[w] = '\0';

    // Trim spaces
    while (text[0] == ' ') memmove(text, text + 1, strlen(text));
    w = strlen(text);
    while (w > 0 && text[w - 1] == ' ') text[--w] = '\0';
    if (w == 0) return false;

    // 2. Enforce word cap & collapse immediate word repeats ("happy happy happy")
    char filtered[256];
    size_t fi = 0;
    char prev1[32] = {};
    char prev2[32] = {};
    size_t word_cnt = 0;

    char copy[256];
    strlcpy(copy, text, sizeof(copy));
    char* saveptr = nullptr;
    char* tok = strtok_r(copy, " ", &saveptr);
    while (tok && word_cnt < max_words) {
        if (strcasecmp(tok, prev1) == 0 && strcasecmp(tok, prev2) == 0) {
            break; // stop trigram repetition loop cleanly
        }
        size_t tlen = strlen(tok);
        if (fi + tlen + 2 >= sizeof(filtered) || fi + tlen + 2 > max_chars) break;
        if (fi > 0) filtered[fi++] = ' ';
        memcpy(filtered + fi, tok, tlen);
        fi += tlen;
        strlcpy(prev2, prev1, sizeof(prev2));
        strlcpy(prev1, tok,   sizeof(prev1));
        word_cnt++;
        tok = strtok_r(nullptr, " ", &saveptr);
    }
    filtered[fi] = '\0';
    if (fi == 0) return false;

    // 3. Final output blocklist check
    char low[256];
    for (size_t i = 0; i <= fi; i++) {
        low[i] = filtered[i] ? (char)tolower((unsigned char)filtered[i]) : '\0';
    }
    for (size_t i = 0; i < sizeof(UNSAFE_KEYWORDS) / sizeof(UNSAFE_KEYWORDS[0]); i++) {
        if (strstr(low, UNSAFE_KEYWORDS[i]) != nullptr) {
            return false;
        }
    }

    strlcpy(text, filtered, max_chars + 1);
    return true;
}
