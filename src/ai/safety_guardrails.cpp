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

static size_t valid_utf8_mb_len(const char* p) {
    uint8_t b0 = (uint8_t)p[0];
    if ((b0 & 0xE0) == 0xC0 && b0 >= 0xC2) {
        uint8_t b1 = (uint8_t)p[1];
        if ((b1 & 0xC0) != 0x80) return 0;
        if (b0 == 0xC2 && b1 < 0xA0) return 0; // reject C1 controls
        return 2;
    }
    if ((b0 & 0xF0) == 0xE0) {
        uint8_t b1 = (uint8_t)p[1];
        if ((b1 & 0xC0) != 0x80) return 0;
        uint8_t b2 = (uint8_t)p[2];
        if ((b2 & 0xC0) != 0x80) return 0;
        if (b0 == 0xE0 && b1 < 0xA0) return 0;
        return 3;
    }
    if ((b0 & 0xF8) == 0xF0 && b0 <= 0xF4) {
        uint8_t b1 = (uint8_t)p[1];
        if ((b1 & 0xC0) != 0x80) return 0;
        uint8_t b2 = (uint8_t)p[2];
        if ((b2 & 0xC0) != 0x80) return 0;
        uint8_t b3 = (uint8_t)p[3];
        if ((b3 & 0xC0) != 0x80) return 0;
        return 4;
    }
    return 0;
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

    // 1. Strip control & prompt-injection delimiter characters, preserve valid UTF-8, clamp to 120 bytes
    size_t j = 0;
    for (size_t i = 0; raw_input[i] && j < LLM_MAX_INPUT_CHARS; i++) {
        unsigned char c = (unsigned char)raw_input[i];
        if (c >= 0x80) {
            size_t mbl = valid_utf8_mb_len(&raw_input[i]);
            if (mbl > 0 && j + mbl <= LLM_MAX_INPUT_CHARS) {
                memcpy(&res.cleanInput[j], &raw_input[i], mbl);
                j += mbl;
                i += mbl - 1;
            }
            continue;
        }
        if (c < 0x20 || c == 0x7F) {
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
        unsigned char sc = (unsigned char)res.cleanInput[i];
        scan[i] = res.cleanInput[i] ? (sc < 0x80 ? normalize_leet(res.cleanInput[i]) : ' ') : '\0';
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

    // 1. Whitelist printable safe ASCII + valid UTF-8 sequences & clamp byte length cleanly
    size_t w = 0;
    for (size_t r = 0; text[r] && w < max_chars; ) {
        unsigned char uc = (unsigned char)text[r];
        if (uc >= 0x80) {
            size_t mbl = valid_utf8_mb_len(&text[r]);
            if (mbl == 0) { r++; continue; }
            if (w + mbl > max_chars) break;
            memcpy(&text[w], &text[r], mbl);
            w += mbl;
            r += mbl;
            continue;
        }
        char c = text[r++];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == ' ' || c == '.' || c == ',' || c == '!' || c == '?' ||
            c == '\'' || c == '-' || c == ':' || c == '(' || c == ')' || c == '=') {
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
    char prev1[64] = {};
    char prev2[64] = {};
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
