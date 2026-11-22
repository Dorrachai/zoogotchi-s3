#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// On-Chip Compact BM25 RAG Engine (/littlefs/rag/kb.bin, PSRAM-cached)
// Zero-allocation hot path except bounded temporary score array on heap.
// =============================================================================

struct RagHit {
    uint16_t chunkId;
    uint8_t  category; // 0=species, 1=dsa, 2=jlpt, 3=genetics, 4=care
    float    score;
    char     title[40];
    char     tag[36];
    char     snippet[160];
};

bool     rag_init();
bool     rag_is_ready();
uint16_t rag_chunk_count();
uint16_t rag_term_count();

// Returns number of matching chunks written to out_hits (<= max_k, max 4)
uint8_t  rag_query(const char* query, const char* species_slug,
                   RagHit* out_hits, uint8_t max_k);
