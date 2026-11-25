#include "rag_retriever.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#pragma pack(push, 1)
struct ZragHeader {
    char     magic[4];    // "ZRAG"
    uint32_t version;     // 1
    uint16_t num_chunks;
    uint16_t num_terms;
    uint16_t avg_dl_x100;
    uint16_t reserved;
    uint32_t chunk_off;
    uint32_t term_off;
    uint32_t post_off;
    uint32_t total_bytes;
    uint32_t crc32;
};

struct RagChunkRec {
    uint16_t id;
    uint8_t  cat;
    uint8_t  doc_len;
    char     title[40];
    char     tag[36];
    char     snippet[160];
};

struct RagTermRec {
    uint32_t hash;
    uint16_t idf_x100;
    uint16_t post_cnt;
    uint32_t post_off;
};

struct RagPostingRec {
    uint16_t chunk_id;
    uint8_t  tf;
    uint8_t  pad;
};
#pragma pack(pop)

static_assert(sizeof(ZragHeader) == 36,    "ZragHeader must be 36 bytes");
static_assert(sizeof(RagChunkRec) == 240,  "RagChunkRec must be 240 bytes");
static_assert(sizeof(RagTermRec) == 12,    "RagTermRec must be 12 bytes");
static_assert(sizeof(RagPostingRec) == 4,  "RagPostingRec must be 4 bytes");

static uint8_t*   _zrag_buf = nullptr;
static ZragHeader _hdr      = {};
static bool       _ready    = false;

static void utf8_strlcpy(char* dst, const char* src, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlcpy(dst, src, dst_size);
    if (n >= dst_size) {
        size_t end = dst_size - 1;
        while (end > 0 && ((uint8_t)dst[end - 1] & 0xC0) == 0x80) end--;
        if (end > 0 && ((uint8_t)dst[end - 1] & 0x80) != 0) end--;
        dst[end] = '\0';
    }
}

static uint32_t ieee_crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

static uint32_t fnv1a_lower(const char* s, size_t len) {
    uint32_t h = 2166136261UL;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
        h ^= c;
        h *= 16777619UL;
    }
    return h;
}

static bool decode_utf8_3(const char* s, uint32_t* out_cp) {
    uint8_t b0 = (uint8_t)s[0];
    if ((b0 & 0xF0) != 0xE0) return false;
    uint8_t b1 = (uint8_t)s[1];
    uint8_t b2 = (uint8_t)s[2];
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return false;
    *out_cp = ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(b1 & 0x3F) << 6) | (uint32_t)(b2 & 0x3F);
    return true;
}

static bool is_jp_kanji(uint32_t cp) {
    return cp >= 0x4E00UL && cp <= 0x9FFFUL;
}

static bool is_jp_char(uint32_t cp) {
    return (cp >= 0x3041UL && cp <= 0x3096UL) ||
           (cp >= 0x30A1UL && cp <= 0x30FAUL) ||
           cp == 0x30FCUL ||
           is_jp_kanji(cp);
}

static const RagTermRec* bsearch_term(uint32_t h) {
    const RagTermRec* terms = reinterpret_cast<const RagTermRec*>(_zrag_buf + _hdr.term_off);
    int32_t lo = 0;
    int32_t hi = (int32_t)_hdr.num_terms - 1;
    while (lo <= hi) {
        int32_t mid = lo + ((hi - lo) >> 1);
        uint32_t mh = terms[mid].hash;
        if (mh == h) return &terms[mid];
        if (mh < h) lo = mid + 1;
        else        hi = mid - 1;
    }
    return nullptr;
}

static void score_term_hash(uint32_t h, uint32_t* seen_hashes, uint8_t* seen_cnt,
                            uint16_t n_chunks, const RagChunkRec* chunks,
                            float avg_dl, float* scores) {
    if (*seen_cnt >= 32) return;
    for (uint8_t s = 0; s < *seen_cnt; s++) {
        if (seen_hashes[s] == h) return;
    }
    seen_hashes[(*seen_cnt)++] = h;

    const RagTermRec* term = bsearch_term(h);
    if (!term) return;
    if ((size_t)_hdr.post_off + term->post_off + (size_t)term->post_cnt * sizeof(RagPostingRec) > _hdr.total_bytes) {
        return;
    }

    const RagPostingRec* posts = reinterpret_cast<const RagPostingRec*>(
        _zrag_buf + _hdr.post_off + term->post_off);
    float idf = (float)term->idf_x100 / 100.0f;

    for (uint16_t p = 0; p < term->post_cnt; p++) {
        uint16_t cid = posts[p].chunk_id;
        if (cid >= n_chunks) continue;
        float tf = (float)posts[p].tf;
        float dl = (float)chunks[cid].doc_len;
        float denom = tf + 1.2f * (0.25f + 0.75f * (dl / avg_dl));
        scores[cid] += idf * ((tf * 2.2f) / denom);
    }
}

bool rag_init() {
    if (_ready) return true;
    const char* path = "/rag/kb.bin";
    if (!LittleFS.exists(path)) {
        Serial.println("[rag] /rag/kb.bin not found in LittleFS");
        return false;
    }
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    size_t fsz = f.size();
    if (fsz < sizeof(ZragHeader) || fsz > 256 * 1024) {
        f.close();
        return false;
    }

    _zrag_buf = (uint8_t*)heap_caps_malloc(fsz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_zrag_buf) _zrag_buf = (uint8_t*)malloc(fsz);
    if (!_zrag_buf) {
        f.close();
        return false;
    }

    size_t got = f.read(_zrag_buf, fsz);
    f.close();
    if (got != fsz) {
        free(_zrag_buf);
        _zrag_buf = nullptr;
        return false;
    }

    memcpy(&_hdr, _zrag_buf, sizeof(ZragHeader));
    if (memcmp(_hdr.magic, "ZRAG", 4) != 0 || _hdr.version != 1 ||
        _hdr.total_bytes != fsz || _hdr.num_chunks == 0 ||
        _hdr.chunk_off < sizeof(ZragHeader) ||
        _hdr.term_off < _hdr.chunk_off ||
        _hdr.post_off < _hdr.term_off) {
        Serial.println("[rag] Invalid ZRAG header");
        free(_zrag_buf);
        _zrag_buf = nullptr;
        return false;
    }

    uint32_t actual_crc = ieee_crc32(_zrag_buf + sizeof(ZragHeader), fsz - sizeof(ZragHeader));
    if (actual_crc != _hdr.crc32) {
        Serial.printf("[rag] CRC mismatch: got 0x%08lX != 0x%08lX\n",
                      (unsigned long)actual_crc, (unsigned long)_hdr.crc32);
        free(_zrag_buf);
        _zrag_buf = nullptr;
        return false;
    }

    _ready = true;
    Serial.printf("[rag] Indexed %u chunks / %u terms (%u B) in PSRAM\n",
                  (unsigned)_hdr.num_chunks, (unsigned)_hdr.num_terms, (unsigned)fsz);
    return true;
}

bool     rag_is_ready()     { return _ready; }
uint16_t rag_chunk_count()  { return _ready ? _hdr.num_chunks : 0; }
uint16_t rag_term_count()   { return _ready ? _hdr.num_terms : 0; }

static uint8_t rag_query_filtered(const char* query, const char* species_slug,
                                  uint8_t cat_mask, float sp_boost,
                                  RagHit* out_hits, uint8_t max_k) {
    if (!_ready || !query || !query[0] || !out_hits || max_k == 0) return 0;
    if (max_k > 8) max_k = 8;

    uint16_t n_chunks = _hdr.num_chunks;
    float* scores = (float*)calloc(n_chunks, sizeof(float));
    if (!scores) return 0;

    float avg_dl = (float)_hdr.avg_dl_x100 / 100.0f;
    if (avg_dl < 1.0f) avg_dl = 1.0f;

    const RagChunkRec* chunks = reinterpret_cast<const RagChunkRec*>(_zrag_buf + _hdr.chunk_off);
    uint32_t seen_hashes[32] = {};
    uint8_t  seen_cnt = 0;

    // Scan both ASCII alphanumeric words and UTF-8 Japanese Kanji/Kana bigrams
    size_t i = 0;
    while (query[i] && seen_cnt < 32) {
        unsigned char c = (unsigned char)query[i];
        if (isalnum(c)) {
            size_t start = i;
            while (query[i] && isalnum((unsigned char)query[i])) i++;
            size_t tok_len = i - start;
            if (tok_len >= 2) {
                uint32_t h = fnv1a_lower(query + start, tok_len);
                score_term_hash(h, seen_hashes, &seen_cnt, n_chunks, chunks, avg_dl, scores);
            }
            continue;
        }
        uint32_t cp = 0;
        if (c >= 0xE0 && query[i + 1] && query[i + 2] && decode_utf8_3(query + i, &cp) && is_jp_char(cp)) {
            size_t prev_off = SIZE_MAX;
            while (query[i] && (unsigned char)query[i] >= 0xE0 &&
                   query[i + 1] && query[i + 2] && decode_utf8_3(query + i, &cp) && is_jp_char(cp)) {
                if (is_jp_kanji(cp)) {
                    uint32_t hk = fnv1a_lower(query + i, 3);
                    score_term_hash(hk, seen_hashes, &seen_cnt, n_chunks, chunks, avg_dl, scores);
                }
                if (prev_off != SIZE_MAX) {
                    char bg[6];
                    memcpy(bg, query + prev_off, 3);
                    memcpy(bg + 3, query + i, 3);
                    uint32_t hb = fnv1a_lower(bg, 6);
                    score_term_hash(hb, seen_hashes, &seen_cnt, n_chunks, chunks, avg_dl, scores);
                }
                prev_off = i;
                i += 3;
            }
            continue;
        }
        i++;
    }

    // Filter out unwanted categories cleanly
    if (cat_mask != 0xFF) {
        for (uint16_t c = 0; c < n_chunks; c++) {
            if (((cat_mask >> chunks[c].cat) & 1U) == 0) {
                scores[c] = 0.0f;
            }
        }
    }

    // Active-species tag relevance boost on matched chunks
    if (species_slug && species_slug[0]) {
        char sp_tag[40];
        snprintf(sp_tag, sizeof(sp_tag), "species:%s", species_slug);
        for (uint16_t c = 0; c < n_chunks; c++) {
            if (scores[c] > 0.0f && strncmp(chunks[c].tag, sp_tag, sizeof(chunks[c].tag)) == 0) {
                scores[c] += sp_boost;
            }
        }
    }

    uint8_t hit_cnt = 0;
    for (uint8_t k = 0; k < max_k; k++) {
        int32_t best_id = -1;
        float   best_sc = 0.0f;
        for (uint16_t c = 0; c < n_chunks; c++) {
            if (scores[c] > best_sc) {
                best_sc = scores[c];
                best_id = (int32_t)c;
            }
        }
        if (best_id < 0) break;

        RagHit& dst = out_hits[hit_cnt++];
        memset(&dst, 0, sizeof(RagHit));
        dst.chunkId  = (uint16_t)best_id;
        dst.category = chunks[best_id].cat;
        dst.score    = best_sc;
        utf8_strlcpy(dst.title,   chunks[best_id].title,   sizeof(dst.title));
        utf8_strlcpy(dst.tag,     chunks[best_id].tag,     sizeof(dst.tag));
        utf8_strlcpy(dst.snippet, chunks[best_id].snippet, sizeof(dst.snippet));
        scores[best_id] = -1.0f;
    }

    free(scores);
    return hit_cnt;
}

uint8_t rag_query(const char* query, const char* species_slug,
                  RagHit* out_hits, uint8_t max_k) {
    return rag_query_filtered(query, species_slug, 0xFF, 0.35f, out_hits, max_k);
}

uint8_t rag_query_zoo(const char* query, const char* species_slug,
                      RagHit* out_hits, uint8_t max_k) {
    // Categories: 0=species, 3=genetics, 4=care (exclude 1=dsa, 2=jlpt)
    static constexpr uint8_t ZOO_CAT_MASK = (1U << 0) | (1U << 3) | (1U << 4);
    return rag_query_filtered(query, species_slug, ZOO_CAT_MASK, 0.85f, out_hits, max_k);
}

uint8_t rag_list_species_chunks(const char* species_slug,
                                RagHit* out_hits, uint8_t max_k) {
    if (!_ready || !species_slug || !species_slug[0] || !out_hits || max_k == 0) return 0;
    if (max_k > 8) max_k = 8;

    char sp_tag[40];
    snprintf(sp_tag, sizeof(sp_tag), "species:%s", species_slug);

    uint16_t n_chunks = _hdr.num_chunks;
    const RagChunkRec* chunks = reinterpret_cast<const RagChunkRec*>(_zrag_buf + _hdr.chunk_off);
    uint8_t hit_cnt = 0;
    for (uint16_t c = 0; c < n_chunks && hit_cnt < max_k; c++) {
        if (strncmp(chunks[c].tag, sp_tag, sizeof(chunks[c].tag)) == 0) {
            RagHit& dst = out_hits[hit_cnt++];
            memset(&dst, 0, sizeof(RagHit));
            dst.chunkId  = chunks[c].id;
            dst.category = chunks[c].cat;
            dst.score    = 1.0f;
            utf8_strlcpy(dst.title,   chunks[c].title,   sizeof(dst.title));
            utf8_strlcpy(dst.tag,     chunks[c].tag,     sizeof(dst.tag));
            utf8_strlcpy(dst.snippet, chunks[c].snippet, sizeof(dst.snippet));
        }
    }
    return hit_cnt;
}
