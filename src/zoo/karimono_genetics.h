#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// Karimono Genetics, Cross-Breeding & Lineage Tree
// =============================================================================

static constexpr uint8_t KARIMONO_MUT_STAR_GLOW    = 0x01;
static constexpr uint8_t KARIMONO_MUT_CRYSTAL_WING = 0x02;
static constexpr uint8_t KARIMONO_MUT_PRISM_PAT    = 0x04;
static constexpr uint8_t LINEAGE_MAX_ENTRIES       = 32;

struct KarimonoGenome {
    uint16_t generation;
    uint8_t  bodyGene;       // 0..5
    uint8_t  earHornGene;    // 0..5
    uint8_t  eyeGene;        // 0..5
    uint8_t  tailWingGene;   // 0..5
    uint8_t  patternGene;    // 0..3
    uint16_t primaryHue;     // 0..359
    uint16_t accentHue;      // 0..359
    uint8_t  mutationMask;   // bits 0..2
    int8_t   traitBias[4];   // -30..+30 inherited predisposition
    uint32_t genomeId;
    char     parentAName[16];
    char     parentBName[16];
};

struct LineageEntry {
    uint16_t generation;
    uint32_t genomeId;
    uint16_t primaryHue;
    uint16_t accentHue;
    uint8_t  bodyGene;
    uint8_t  earHornGene;
    uint8_t  tailWingGene;
    uint8_t  mutationMask;
    char     name[16];
    char     parentA[16];
    char     parentB[16];
    int64_t  bornEpoch;
};

KarimonoGenome karimono_starter_genome(uint8_t starter_idx);
KarimonoGenome karimono_generate_plaza_partner(uint16_t current_gen, uint32_t seed);
KarimonoGenome karimono_breed(const KarimonoGenome& a, const char* name_a,
                              const KarimonoGenome& b, const char* name_b,
                              uint32_t seed);

// 12-character Base32 code + 4-bit checksum for sharing / cross-breeding
void karimono_encode_code(const KarimonoGenome& g, char out_code[13]);
bool karimono_decode_code(const char* code, KarimonoGenome* out_g);

// Persistent lineage storage (/littlefs/lineage.json)
bool    lineage_append(const LineageEntry& entry);
uint8_t lineage_load(LineageEntry* out_arr, uint8_t max_count);
uint8_t lineage_count();
