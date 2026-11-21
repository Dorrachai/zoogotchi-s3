#include "karimono_genetics.h"
#include <string.h>
#include <stdio.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static uint32_t xorshift32(uint32_t& s) {
    if (s == 0) s = 0xA5A5F00DUL;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

static uint16_t blend_hue(uint16_t h1, uint16_t h2, int16_t jitter) {
    int diff = (int)h2 - (int)h1;
    if (diff > 180)  diff -= 360;
    if (diff < -180) diff += 360;
    int mid = (int)h1 + diff / 2 + (int)jitter;
    mid %= 360;
    if (mid < 0) mid += 360;
    return (uint16_t)mid;
}

static int8_t clamp_bias(int v) {
    if (v < -30) return -30;
    if (v > 30)  return 30;
    return (int8_t)v;
}

KarimonoGenome karimono_starter_genome(uint8_t idx) {
    KarimonoGenome g = {};
    g.generation = 1;
    strlcpy(g.parentAName, "Wild Shrine", sizeof(g.parentAName));
    strlcpy(g.parentBName, "Origin",      sizeof(g.parentBName));

    switch (idx % 3) {
    case 0: // Hoshi-Karimono
        g.bodyGene     = 3; // Star-Blob
        g.earHornGene  = 4; // Halo-Antenna
        g.eyeGene      = 2; // Starry
        g.tailWingGene = 4; // Star-Tail
        g.patternGene  = 3; // Star-Freckles
        g.primaryHue   = 46;
        g.accentHue    = 268;
        g.traitBias[0] = 10; g.traitBias[1] = 8; g.traitBias[2] = 6; g.traitBias[3] = 10;
        g.genomeId     = 0x484F5331;
        break;
    case 1: // Kumo-Karimono
        g.bodyGene     = 4; // Long-Cloud
        g.earHornGene  = 2; // Bunny-Ears
        g.eyeGene      = 1; // Anime-Sparkle
        g.tailWingGene = 2; // Angel-Wings
        g.patternGene  = 1; // Belly-Patch
        g.primaryHue   = 204;
        g.accentHue    = 330;
        g.traitBias[0] = -8; g.traitBias[1] = -4; g.traitBias[2] = 15; g.traitBias[3] = -6;
        g.genomeId     = 0x4B554D31;
        break;
    default: // Kuro-Karimono
        g.bodyGene     = 5; // Horned-Puff
        g.earHornGene  = 3; // Twin-Horns
        g.eyeGene      = 5; // Pixel-Visor
        g.tailWingGene = 3; // Bat-Wings
        g.patternGene  = 2; // Stripes
        g.primaryHue   = 12;
        g.accentHue    = 38;
        g.traitBias[0] = 15; g.traitBias[1] = 15; g.traitBias[2] = -5; g.traitBias[3] = 8;
        g.genomeId     = 0x4B555231;
        break;
    }
    return g;
}

KarimonoGenome karimono_generate_plaza_partner(uint16_t current_gen, uint32_t seed) {
    uint32_t r = seed ^ 0x9E3779B9UL;
    KarimonoGenome g = {};
    g.generation   = current_gen;
    g.bodyGene     = (uint8_t)(xorshift32(r) % 6);
    g.earHornGene  = (uint8_t)(xorshift32(r) % 6);
    g.eyeGene      = (uint8_t)(xorshift32(r) % 6);
    g.tailWingGene = (uint8_t)(xorshift32(r) % 6);
    g.patternGene  = (uint8_t)(xorshift32(r) % 4);
    g.primaryHue   = (uint16_t)(xorshift32(r) % 360);
    g.accentHue    = (uint16_t)(xorshift32(r) % 360);
    g.mutationMask = ((xorshift32(r) % 100) < 12) ? (uint8_t)(1U << (xorshift32(r) % 3)) : 0;
    for (int i = 0; i < 4; i++) {
        g.traitBias[i] = (int8_t)((int)(xorshift32(r) % 31) - 15);
    }
    g.genomeId = xorshift32(r);
    strlcpy(g.parentAName, "Plaza Spirit", sizeof(g.parentAName));
    strlcpy(g.parentBName, "Shrine",       sizeof(g.parentBName));
    return g;
}

KarimonoGenome karimono_breed(const KarimonoGenome& a, const char* name_a,
                              const KarimonoGenome& b, const char* name_b,
                              uint32_t seed) {
    uint32_t r = seed ^ a.genomeId ^ b.genomeId ^ 0xCAFEBABEUL;
    KarimonoGenome child = {};

    child.generation   = (a.generation > b.generation ? a.generation : b.generation) + 1;
    child.bodyGene     = (xorshift32(r) & 1) ? a.bodyGene     : b.bodyGene;
    child.earHornGene  = (xorshift32(r) & 1) ? a.earHornGene  : b.earHornGene;
    child.eyeGene      = (xorshift32(r) & 1) ? a.eyeGene      : b.eyeGene;
    child.tailWingGene = (xorshift32(r) & 1) ? a.tailWingGene : b.tailWingGene;
    child.patternGene  = (xorshift32(r) & 1) ? a.patternGene  : b.patternGene;

    int16_t jit1 = (int16_t)((int)(xorshift32(r) % 37) - 18);
    int16_t jit2 = (int16_t)((int)(xorshift32(r) % 41) - 20);
    child.primaryHue = blend_hue(a.primaryHue, b.primaryHue, jit1);
    child.accentHue  = blend_hue(a.accentHue,  b.accentHue,  jit2);

    // Inherit mutations (35% chance per parent mutation) + 8% fresh mutation roll
    uint8_t inherited = a.mutationMask | b.mutationMask;
    uint8_t mut = 0;
    for (uint8_t bit = 0; bit < 3; bit++) {
        if ((inherited & (1U << bit)) && ((xorshift32(r) % 100) < 35)) {
            mut |= (uint8_t)(1U << bit);
        }
    }
    if ((xorshift32(r) % 100) < 8) {
        mut |= (uint8_t)(1U << (xorshift32(r) % 3));
    }
    child.mutationMask = mut;

    for (int i = 0; i < 4; i++) {
        int avg = ((int)a.traitBias[i] + (int)b.traitBias[i]) / 2;
        int jit = (int)(xorshift32(r) % 9) - 4;
        child.traitBias[i] = clamp_bias(avg + jit);
    }

    child.genomeId = xorshift32(r);
    strlcpy(child.parentAName, (name_a && name_a[0]) ? name_a : "Parent A", sizeof(child.parentAName));
    strlcpy(child.parentBName, (name_b && name_b[0]) ? name_b : "Parent B", sizeof(child.parentBName));
    return child;
}

static const char B32_ALPHA[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // 32 unambiguous chars

void karimono_encode_code(const KarimonoGenome& g, char out_code[13]) {
    // Pack 55 bits into 11 Base32 chars + 1 CRC5 check char = 12 chars + NUL
    uint64_t payload = 0;
    payload |= (uint64_t)(g.bodyGene & 0x07)         << 0;  // 3 bits
    payload |= (uint64_t)(g.earHornGene & 0x07)      << 3;  // 3 bits
    payload |= (uint64_t)(g.eyeGene & 0x07)          << 6;  // 3 bits
    payload |= (uint64_t)(g.tailWingGene & 0x07)     << 9;  // 3 bits
    payload |= (uint64_t)(g.patternGene & 0x03)      << 12; // 2 bits
    payload |= (uint64_t)(g.mutationMask & 0x07)     << 14; // 3 bits
    payload |= (uint64_t)((g.primaryHue / 2) & 0xFF) << 17; // 8 bits (2-deg step)
    payload |= (uint64_t)((g.accentHue  / 2) & 0xFF) << 25; // 8 bits (2-deg step)
    payload |= (uint64_t)(g.generation & 0xFF)       << 33; // 8 bits
    payload |= (uint64_t)(g.genomeId & 0x3FFF)       << 41; // 14 bits

    uint8_t chk = 0;
    for (int i = 0; i < 11; i++) {
        uint8_t idx = (uint8_t)((payload >> (i * 5)) & 0x1F);
        out_code[i] = B32_ALPHA[idx];
        chk = (uint8_t)((chk * 7 + idx + i) & 0x1F);
    }
    out_code[11] = B32_ALPHA[chk];
    out_code[12] = '\0';
}

static int b32_index(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c == '0') c = 'O';
    if (c == '1') c = 'I';
    for (int i = 0; i < 32; i++) {
        if (B32_ALPHA[i] == c) return i;
    }
    return -1;
}

bool karimono_decode_code(const char* code, KarimonoGenome* out_g) {
    if (!code || !out_g) return false;
    char clean[13];
    int n = 0;
    for (int i = 0; code[i] && n < 12; i++) {
        if (code[i] == '-' || code[i] == ' ') continue;
        clean[n++] = code[i];
    }
    if (n != 12) return false;

    uint64_t payload = 0;
    uint8_t chk = 0;
    for (int i = 0; i < 11; i++) {
        int idx = b32_index(clean[i]);
        if (idx < 0) return false;
        payload |= ((uint64_t)idx) << (i * 5);
        chk = (uint8_t)((chk * 7 + idx + i) & 0x1F);
    }
    int cidx = b32_index(clean[11]);
    if (cidx != chk) return false;

    memset(out_g, 0, sizeof(KarimonoGenome));
    out_g->bodyGene     = (uint8_t)((payload >> 0)  & 0x07) % 6;
    out_g->earHornGene  = (uint8_t)((payload >> 3)  & 0x07) % 6;
    out_g->eyeGene      = (uint8_t)((payload >> 6)  & 0x07) % 6;
    out_g->tailWingGene = (uint8_t)((payload >> 9)  & 0x07) % 6;
    out_g->patternGene  = (uint8_t)((payload >> 12) & 0x03) % 4;
    out_g->mutationMask = (uint8_t)((payload >> 14) & 0x07);
    out_g->primaryHue   = (uint16_t)(((payload >> 17) & 0xFF) * 2 % 360);
    out_g->accentHue    = (uint16_t)(((payload >> 25) & 0xFF) * 2 % 360);
    out_g->generation   = (uint16_t)((payload >> 33) & 0xFF);
    if (out_g->generation == 0) out_g->generation = 1;
    out_g->genomeId     = (uint32_t)((payload >> 41) & 0x3FFF) | 0xA0000000UL;
    strlcpy(out_g->parentAName, "Code Partner", sizeof(out_g->parentAName));
    strlcpy(out_g->parentBName, "Code",         sizeof(out_g->parentBName));
    return true;
}

static const char* LINEAGE_PATH = "/lineage.json";
static const char* LINEAGE_TMP  = "/lineage.tmp";

bool lineage_append(const LineageEntry& entry) {
    LineageEntry buf[LINEAGE_MAX_ENTRIES];
    uint8_t count = lineage_load(buf, LINEAGE_MAX_ENTRIES);

    if (count >= LINEAGE_MAX_ENTRIES) {
        memmove(&buf[0], &buf[1], sizeof(LineageEntry) * (LINEAGE_MAX_ENTRIES - 1));
        count = LINEAGE_MAX_ENTRIES - 1;
    }
    buf[count++] = entry;

    JsonDocument doc;
    JsonArray arr = doc["entries"].to<JsonArray>();
    for (uint8_t i = 0; i < count; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["gen"]  = buf[i].generation;
        o["id"]   = buf[i].genomeId;
        o["ph"]   = buf[i].primaryHue;
        o["ah"]   = buf[i].accentHue;
        o["body"] = buf[i].bodyGene;
        o["ear"]  = buf[i].earHornGene;
        o["tail"] = buf[i].tailWingGene;
        o["mut"]  = buf[i].mutationMask;
        o["name"] = buf[i].name;
        o["pa"]   = buf[i].parentA;
        o["pb"]   = buf[i].parentB;
        o["ts"]   = buf[i].bornEpoch;
    }

    File f = LittleFS.open(LINEAGE_TMP, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    if (LittleFS.exists(LINEAGE_PATH)) LittleFS.remove(LINEAGE_PATH);
    return LittleFS.rename(LINEAGE_TMP, LINEAGE_PATH);
}

uint8_t lineage_load(LineageEntry* out_arr, uint8_t max_count) {
    if (!out_arr || max_count == 0 || !LittleFS.exists(LINEAGE_PATH)) return 0;
    File f = LittleFS.open(LINEAGE_PATH, "r");
    if (!f) return 0;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err || !doc["entries"].is<JsonArray>()) return 0;

    uint8_t n = 0;
    for (JsonObject o : doc["entries"].as<JsonArray>()) {
        if (n >= max_count) break;
        LineageEntry& e = out_arr[n++];
        memset(&e, 0, sizeof(e));
        e.generation   = o["gen"]  | 1;
        e.genomeId     = o["id"]   | 0;
        e.primaryHue   = o["ph"]   | 40;
        e.accentHue    = o["ah"]   | 200;
        e.bodyGene     = o["body"] | 0;
        e.earHornGene  = o["ear"]  | 0;
        e.tailWingGene = o["tail"] | 0;
        e.mutationMask = o["mut"]  | 0;
        e.bornEpoch    = o["ts"]   | 0;
        strlcpy(e.name,    o["name"] | "Karimono", sizeof(e.name));
        strlcpy(e.parentA, o["pa"]   | "Origin",   sizeof(e.parentA));
        strlcpy(e.parentB, o["pb"]   | "Origin",   sizeof(e.parentB));
    }
    return n;
}

uint8_t lineage_count() {
    LineageEntry tmp[LINEAGE_MAX_ENTRIES];
    return lineage_load(tmp, LINEAGE_MAX_ENTRIES);
}
