#include "micro_llm_ple.h"
#include "rag_retriever.h"
#include "../../include/zoo_vocab_out.h"
#include "../config.h"
#include "../power/sleep_controller.h"
#include <Arduino.h>
#include <esp_partition.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

// =============================================================================
// ZPLE Binary Header (matches llm_training/train_ple_zoopet.ipynb)
// =============================================================================
#pragma pack(push, 1)
struct ZpleHeader {
    char     magic[4];       // "ZPLE"
    uint32_t version;        // 1
    uint16_t d_model;
    uint16_t n_layers;
    uint16_t n_heads;
    uint16_t n_kv_heads;
    uint16_t hidden_dim;
    uint16_t max_seq_len;
    uint16_t v_in;
    uint16_t v_out;
    uint16_t group_size;
    uint16_t ple_dim;
    uint32_t vocab_offset;
    uint32_t ple_offset;
    uint32_t dense_offset;
    uint32_t head_offset;
    uint32_t total_bytes;
    uint32_t crc32;
    uint8_t  reserved[12];
};
#pragma pack(pop)
static_assert(sizeof(ZpleHeader) == 64, "ZpleHeader must be 64 bytes");

static const uint8_t*             _flash_mmap_ptr = nullptr;
static spi_flash_mmap_handle_t    _flash_mmap_hdl = 0;
static ZpleHeader                 _hdr            = {};
static bool                       _model_loaded   = false;
static volatile bool              _busy           = false;

// PSRAM buffers for dense core, output head, and KV cache
static uint8_t* _psram_dense = nullptr;
static uint8_t* _psram_head  = nullptr;
static float*   _k_cache     = nullptr;
static float*   _v_cache     = nullptr;

// Internal SRAM scratch buffers
static float*   _x           = nullptr;
static float*   _xb          = nullptr;
static float*   _q           = nullptr;
static float*   _k           = nullptr;
static float*   _v           = nullptr;
static float*   _att         = nullptr;
static float*   _h1          = nullptr;
static float*   _h2          = nullptr;
static float*   _logits      = nullptr;

// Async request queue for Core 0 task
struct BrainRequest {
    ZooPetState  pet;
    PetMemory    mem;
    uint8_t      local_hour;
    uint8_t      battery_pct;
    char         user_msg[LLM_MAX_INPUT_CHARS + 1];
    bool         screen_bubble;
    BrainTokenCb cb;
    void*        user_ctx;
};

static QueueHandle_t _req_queue = nullptr;

// =============================================================================
// IEEE754 fp16 -> float32 helper (for group-128 INT4 scale decoding)
// =============================================================================
static inline float fp16_to_f32(uint16_t h) {
    uint32_t sign = ((uint32_t)h & 0x8000U) << 16;
    uint32_t exp  = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x03FF;
    uint32_t f;
    if (exp == 0) {
        if (mant == 0) {
            f = sign;
        } else {
            exp = 1;
            while ((mant & 0x0400) == 0) { mant <<= 1; exp--; }
            mant &= 0x03FF;
            f = sign | ((exp + 112) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        f = sign | 0x7F800000U | (mant << 13);
    } else {
        f = sign | ((exp + 112) << 23) | (mant << 13);
    }
    float out;
    memcpy(&out, &f, sizeof(out));
    return out;
}

static void rmsnorm(float* o, const float* x, const float* w, int size) {
    float ss = 0.0f;
    for (int j = 0; j < size; j++) ss += x[j] * x[j];
    ss = 1.0f / sqrtf(ss / (float)size + 1e-5f);
    for (int j = 0; j < size; j++) o[j] = w[j] * (ss * x[j]);
}

// Group-wise symmetric signed INT4 (nibble in [-8..+7] stored +8 in [0..15]) matmul
// Layout per row: [group_size/2 uint8 bytes + uint16_t fp16 scale] repeated
static void q4_matmul(float* out, const float* x, const uint8_t* w_base,
                      int rows, int cols, uint16_t gs) {
    int groups_per_row = (cols + gs - 1) / gs;
    size_t grp_bytes   = (gs / 2) + 2;
    size_t row_bytes   = (size_t)groups_per_row * grp_bytes;

    for (int r = 0; r < rows; r++) {
        const uint8_t* rp = w_base + (size_t)r * row_bytes;
        float sum = 0.0f;
        for (int g = 0; g < groups_per_row; g++) {
            const uint8_t* gp = rp + (size_t)g * grp_bytes;
            uint16_t raw_sc;
            memcpy(&raw_sc, gp + (gs / 2), 2);
            float scale = fp16_to_f32(raw_sc);

            int base_c = g * gs;
            int lim    = base_c + gs;
            if (lim > cols) lim = cols;

            float gsum = 0.0f;
            for (int c = base_c; c < lim; c += 2) {
                uint8_t packed = gp[(c - base_c) >> 1];
                int8_t q0 = (int8_t)(packed & 0x0F) - 8;
                int8_t q1 = (int8_t)((packed >> 4) & 0x0F) - 8;
                gsum += (float)q0 * x[c];
                if (c + 1 < lim) gsum += (float)q1 * x[c + 1];
            }
            sum += gsum * scale;
        }
        out[r] = sum;
    }
}

static size_t q4_matrix_bytes(int rows, int cols, uint16_t gs) {
    int groups_per_row = (cols + gs - 1) / gs;
    return (size_t)rows * (size_t)groups_per_row * ((gs / 2) + 2);
}

static bool try_mount_zple_partition() {
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, LLM_PARTITION_LABEL);
    if (!part) {
        Serial.println("[llm] No llm_model partition found — fallback brain active");
        return false;
    }
    if (esp_partition_mmap(part, 0, part->size, SPI_FLASH_MMAP_DATA,
                           (const void**)&_flash_mmap_ptr, &_flash_mmap_hdl) != ESP_OK) {
        Serial.println("[llm] mmap failed — fallback brain active");
        return false;
    }

    memcpy(&_hdr, _flash_mmap_ptr, sizeof(ZpleHeader));
    if (memcmp(_hdr.magic, "ZPLE", 4) != 0 || _hdr.version != 1 ||
        _hdr.v_out != ZOO_VOUT_COUNT || _hdr.d_model == 0 || _hdr.n_layers == 0 ||
        _hdr.total_bytes > part->size) {
        Serial.println("[llm] llm_model empty or unprogrammed — using rich personality fallback");
        return false;
    }

    size_t dense_sz = _hdr.head_offset - _hdr.dense_offset;
    size_t head_sz  = _hdr.total_bytes - _hdr.head_offset;
    size_t kv_dim   = (_hdr.d_model * _hdr.n_kv_heads) / _hdr.n_heads;
    size_t kv_sz    = (size_t)_hdr.n_layers * _hdr.max_seq_len * kv_dim * sizeof(float);

    _psram_dense = (uint8_t*)heap_caps_malloc(dense_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    _psram_head  = (uint8_t*)heap_caps_malloc(head_sz,  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    _k_cache     = (float*)heap_caps_calloc(1, kv_sz,   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    _v_cache     = (float*)heap_caps_calloc(1, kv_sz,   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    _x      = (float*)malloc(_hdr.d_model * sizeof(float));
    _xb     = (float*)malloc(_hdr.d_model * sizeof(float));
    _q      = (float*)malloc(_hdr.d_model * sizeof(float));
    _k      = (float*)malloc(kv_dim * sizeof(float));
    _v      = (float*)malloc(kv_dim * sizeof(float));
    _att    = (float*)malloc(_hdr.n_heads * _hdr.max_seq_len * sizeof(float));
    _h1     = (float*)malloc(_hdr.hidden_dim * sizeof(float));
    _h2     = (float*)malloc(_hdr.hidden_dim * sizeof(float));
    _logits = (float*)malloc(_hdr.v_out * sizeof(float));

    if (!_psram_dense || !_psram_head || !_k_cache || !_v_cache ||
        !_x || !_xb || !_q || !_k || !_v || !_att || !_h1 || !_h2 || !_logits) {
        Serial.println("[llm] Allocation failed — using fallback brain");
        return false;
    }

    memcpy(_psram_dense, _flash_mmap_ptr + _hdr.dense_offset, dense_sz);
    memcpy(_psram_head,  _flash_mmap_ptr + _hdr.head_offset,  head_sz);
    Serial.printf("[llm] ZPLE model mounted! d=%u L=%u v_in=%u v_out=%u\n",
                  _hdr.d_model, _hdr.n_layers, _hdr.v_in, _hdr.v_out);
    return true;
}

// Hash input words into V_in IDs matching train_ple_zoopet.ipynb FNV-1a token table
static uint16_t hash_word_to_vin(const char* word, uint16_t v_in) {
    uint32_t h = 2166136261UL;
    for (int i = 0; word[i]; i++) {
        char c = (word[i] >= 'A' && word[i] <= 'Z') ? (char)(word[i] + 32) : word[i];
        h ^= (uint8_t)c;
        h *= 16777619UL;
    }
    return (uint16_t)(4 + (h % (v_in > 4 ? (v_in - 4) : 1)));
}

static bool ple_forward_step(uint16_t token_in, int pos, int max_out_logits, float* out_logits) {
    if (!_model_loaded || pos >= _hdr.max_seq_len) return false;

    int D   = _hdr.d_model;
    int H   = _hdr.hidden_dim;
    int nh  = _hdr.n_heads;
    int nkv = _hdr.n_kv_heads;
    int hd  = D / nh;
    int kvd = hd * nkv;
    uint16_t gs = _hdr.group_size;

    // Fetch token row from Flash mmap Per-Layer Embedding table
    size_t ple_row_bytes = (size_t)_hdr.n_layers * q4_matrix_bytes(1, D, gs);
    const uint8_t* ple_tok = _flash_mmap_ptr + _hdr.ple_offset + (size_t)(token_in % _hdr.v_in) * ple_row_bytes;

    // Seed x from layer-0 embedding row
    memset(_x, 0, D * sizeof(float));
    const uint8_t* ptr = _psram_dense;

    size_t wq_sz = q4_matrix_bytes(D, D, gs);
    size_t wk_sz = q4_matrix_bytes(kvd, D, gs);
    size_t wv_sz = q4_matrix_bytes(kvd, D, gs);
    size_t wo_sz = q4_matrix_bytes(D, D, gs);
    size_t w1_sz = q4_matrix_bytes(H, D, gs);
    size_t w3_sz = q4_matrix_bytes(H, D, gs);
    size_t w2_sz = q4_matrix_bytes(D, H, gs);
    size_t norm_sz = (size_t)D * sizeof(float);

    // Layer-by-layer forward with Per-Layer Embedding addition
    for (int l = 0; l < _hdr.n_layers; l++) {
        // Dequantize tiny PLE slice from Flash directly into _x residual
        const uint8_t* l_ple = ple_tok + (size_t)l * q4_matrix_bytes(1, D, gs);
        int grps = (D + gs - 1) / gs;
        size_t gbytes = (gs / 2) + 2;
        for (int g = 0; g < grps; g++) {
            const uint8_t* gp = l_ple + (size_t)g * gbytes;
            uint16_t raw_sc;
            memcpy(&raw_sc, gp + (gs / 2), 2);
            float sc = fp16_to_f32(raw_sc);
            int bc = g * gs;
            for (int c = bc; c < bc + gs && c < D; c += 2) {
                uint8_t pk = gp[(c - bc) >> 1];
                _x[c] += ((int8_t)(pk & 0x0F) - 8) * sc;
                if (c + 1 < D) _x[c + 1] += ((int8_t)((pk >> 4) & 0x0F) - 8) * sc;
            }
        }

        const float* att_norm = (const float*)ptr; ptr += norm_sz;
        const uint8_t* wq = ptr; ptr += wq_sz;
        const uint8_t* wk = ptr; ptr += wk_sz;
        const uint8_t* wv = ptr; ptr += wv_sz;
        const uint8_t* wo = ptr; ptr += wo_sz;
        const float* ffn_norm = (const float*)ptr; ptr += norm_sz;
        const uint8_t* w1 = ptr; ptr += w1_sz;
        const uint8_t* w3 = ptr; ptr += w3_sz;
        const uint8_t* w2 = ptr; ptr += w2_sz;

        rmsnorm(_xb, _x, att_norm, D);
        q4_matmul(_q, _xb, wq, D, D, gs);
        q4_matmul(_k, _xb, wk, kvd, D, gs);
        q4_matmul(_v, _xb, wv, kvd, D, gs);

        // RoPE
        for (int i = 0; i < D; i += 2) {
            int head_dim = i % hd;
            float freq = 1.0f / powf(10000.0f, (float)head_dim / (float)hd);
            float val  = (float)pos * freq;
            float fcr  = cosf(val), fci = sinf(val);
            float q0 = _q[i], q1 = _q[i + 1];
            _q[i]     = q0 * fcr - q1 * fci;
            _q[i + 1] = q0 * fci + q1 * fcr;
            if (i < kvd) {
                float k0 = _k[i], k1 = _k[i + 1];
                _k[i]     = k0 * fcr - k1 * fci;
                _k[i + 1] = k0 * fci + k1 * fcr;
            }
        }

        size_t layer_off = (size_t)l * _hdr.max_seq_len * kvd;
        memcpy(_k_cache + layer_off + (size_t)pos * kvd, _k, kvd * sizeof(float));
        memcpy(_v_cache + layer_off + (size_t)pos * kvd, _v, kvd * sizeof(float));

        int kv_mul = nh / nkv;
        for (int h = 0; h < nh; h++) {
            float* qh  = _q + h * hd;
            float* att = _att + h * _hdr.max_seq_len;
            int kvh    = h / kv_mul;
            float mx   = -1e9f;
            for (int t = 0; t <= pos; t++) {
                const float* kt = _k_cache + layer_off + (size_t)t * kvd + kvh * hd;
                float sc = 0.0f;
                for (int i = 0; i < hd; i++) sc += qh[i] * kt[i];
                sc /= sqrtf((float)hd);
                att[t] = sc;
                if (sc > mx) mx = sc;
            }
            float sum = 0.0f;
            for (int t = 0; t <= pos; t++) {
                att[t] = expf(att[t] - mx);
                sum += att[t];
            }
            for (int t = 0; t <= pos; t++) att[t] /= sum;
            float* xbh = _xb + h * hd;
            memset(xbh, 0, hd * sizeof(float));
            for (int t = 0; t <= pos; t++) {
                const float* vt = _v_cache + layer_off + (size_t)t * kvd + kvh * hd;
                float a = att[t];
                for (int i = 0; i < hd; i++) xbh[i] += a * vt[i];
            }
        }
        q4_matmul(_h1, _xb, wo, D, D, gs);
        for (int i = 0; i < D; i++) _x[i] += _h1[i];

        // SwiGLU FFN
        rmsnorm(_xb, _x, ffn_norm, D);
        q4_matmul(_h1, _xb, w1, H, D, gs);
        q4_matmul(_h2, _xb, w3, H, D, gs);
        for (int i = 0; i < H; i++) {
            float val = _h1[i];
            val *= (1.0f / (1.0f + expf(-val)));
            _h1[i] = val * _h2[i];
        }
        q4_matmul(_xb, _h1, w2, D, H, gs);
        for (int i = 0; i < D; i++) _x[i] += _xb[i];
    }

    if (out_logits && max_out_logits > 0) {
        const float* out_norm = (const float*)ptr;
        rmsnorm(_xb, _x, out_norm, D);
        q4_matmul(out_logits, _xb, _psram_head, max_out_logits, D, gs);
    }
    return true;
}

static uint16_t sample_top_k(const float* logits, int n, float temp) {
    if (temp <= 0.05f) {
        int best = 4;
        for (int i = 5; i < n; i++) if (logits[i] > logits[best]) best = i;
        return (uint16_t)best;
    }
    int top_idx[8] = {4, 4, 4, 4, 4, 4, 4, 4};
    float top_val[8] = {-1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f};
    for (int i = 2; i < n; i++) { // allow EOS=2 and real vocab >=4
        if (i == 3) continue; // skip <unk>
        float v = logits[i];
        if (v > top_val[7]) {
            top_val[7] = v;
            top_idx[7] = i;
            for (int k = 7; k > 0 && top_val[k] > top_val[k - 1]; k--) {
                float tv = top_val[k - 1]; top_val[k - 1] = top_val[k]; top_val[k] = tv;
                int ti = top_idx[k - 1];   top_idx[k - 1] = top_idx[k]; top_idx[k] = ti;
            }
        }
    }
    float probs[8];
    float sum = 0.0f;
    for (int k = 0; k < 8; k++) {
        probs[k] = expf((top_val[k] - top_val[0]) / temp);
        sum += probs[k];
    }
    float r = ((float)(esp_random() % 10000) / 10000.0f) * sum;
    float acc = 0.0f;
    for (int k = 0; k < 8; k++) {
        acc += probs[k];
        if (r <= acc) return (uint16_t)top_idx[k];
    }
    return (uint16_t)top_idx[0];
}

// =============================================================================
// Structured Personality Fallback Engine (Always Available, Safe, Expressive)
// =============================================================================
static void generate_fallback_reply(const ZooPetState* pet, const PetMemory* mem,
                                    uint8_t local_hour, const char* clean_input,
                                    const RagHit* rag_hit,
                                    bool screen_bubble, char* out, size_t out_len) {
    const SpeciesProfile& sp = species_get(pet->speciesId);
    PetMood mood = zoo_compute_mood(pet, millis());
    uint32_t r = esp_random();

    char low[LLM_MAX_INPUT_CHARS + 1];
    size_t ilen = strlen(clean_input);
    for (size_t i = 0; i <= ilen; i++) {
        low[i] = clean_input[i] ? (char)tolower((unsigned char)clean_input[i]) : '\0';
    }

    const char* trait_adj = (pet->playfulCalm >= 20) ? "bounces happily"
                          : (pet->playfulCalm <= -20) ? "settles peacefully"
                          : (pet->affectionIndep >= 20) ? "nuzzles closer"
                          : "blinks curiously";

    if (pet->isDead) {
        snprintf(out, out_len, "%s's memory shines gently in the Sanctuary.", pet->petName);
        return;
    }

    if (rag_hit && rag_hit->score >= 2.2f) {
        if (screen_bubble) {
            snprintf(out, out_len, "%s: %s", rag_hit->tag, rag_hit->snippet);
        } else {
            snprintf(out, out_len, "%s recalls (%s): %s", pet->petName, rag_hit->tag, rag_hit->snippet);
        }
        size_t max_w = screen_bubble ? LLM_MAX_TOK_SCREEN : LLM_MAX_TOK_WEB;
        size_t max_c = screen_bubble ? 110 : 220;
        safety_validate_output(out, max_w, max_c < out_len - 1 ? max_c : out_len - 1);
        return;
    }

    if (strstr(low, "study") || strstr(low, "quiz") || strstr(low, "leetcode") ||
        strstr(low, "dsa") || strstr(low, "word") || strstr(low, "learn")) {
        if (mem && mem->lastLearned[0]) {
            snprintf(out, out_len, "%s %s! Remember '%s'? Let's do a quick review!",
                     pet->petName, trait_adj, mem->lastLearned);
        } else {
            snprintf(out, out_len, "%s puts on tiny study glasses! Ready for a quick quiz?",
                     pet->petName);
        }
    } else if (strstr(low, "fact") || strstr(low, "about")) {
        snprintf(out, out_len, "%s: %s", pet->petName, sp.funFacts[r % 3]);
    } else if (strstr(low, "food") || strstr(low, "hungry") || strstr(low, "eat") || strstr(low, "snack")) {
        if (pet->hunger < 45) {
            snprintf(out, out_len, "%s tummy rumbles for %s!", pet->petName, sp.primaryFood);
        } else {
            snprintf(out, out_len, "%s %s and dreams of %s.", pet->petName, trait_adj, sp.treatFood);
        }
    } else if (strstr(low, "breed") || strstr(low, "family") || strstr(low, "lineage")) {
        if (sp.isKarimono) {
            snprintf(out, out_len, "%s (Gen %u) glows with pride from %s!",
                     pet->petName, (unsigned)pet->genome.generation, pet->genome.parentAName);
        } else {
            snprintf(out, out_len, "%s is a real %s thriving in %s!",
                     pet->petName, sp.name, sp.biomeName);
        }
    } else if (strstr(low, "walk") || strstr(low, "step")) {
        snprintf(out, out_len, "%s loved our %lu steps today!",
                 pet->petName, (unsigned long)pet->stepsToday);
    } else if (strstr(low, "how") || strstr(low, "feel") || strstr(low, "mood")) {
        if (mood == PetMood::SLEEPING) {
            snprintf(out, out_len, "%s yawns softly in %s... zzz.", pet->petName, sp.biomeName);
        } else if (mood == PetMood::SICK) {
            snprintf(out, out_len, "%s feels a little unwell. Some %s or tonic helps!",
                     pet->petName, sp.envCareLabel);
        } else if (mood == PetMood::ECSTATIC) {
            snprintf(out, out_len, "%s %s! Bond %u%% with you, %s!",
                     pet->petName, trait_adj, (unsigned)pet->bond, pet->keeperName);
        } else {
            snprintf(out, out_len, "%s is %s in %s. Last: %s.",
                     pet->petName, zoo_mood_name(mood), sp.biomeName,
                     (mem && mem->lastAction[0]) ? mem->lastAction : "exploring");
        }
    } else if (strstr(low, "pet") || strstr(low, "hug") || strstr(low, "love") || strstr(low, "hi") || strstr(low, "hello")) {
        if (sp.id == 9) { // Manatee special warmth
            snprintf(out, out_len, "%s paddles gently in warm lagoon water and squeaks hello, %s!",
                     pet->petName, pet->keeperName);
        } else if (sp.isKarimono) {
            snprintf(out, out_len, "%s (Gen %u) %s and sends starry sparks!",
                     pet->petName, (unsigned)pet->genome.generation, trait_adj);
        } else {
            snprintf(out, out_len, "%s %s! Happy to see you, %s!",
                     pet->petName, trait_adj, pet->keeperName);
        }
    } else {
        // Contextual idle/conversational reflection using circadian + memory
        if (mood == PetMood::SLEEPING) {
            snprintf(out, out_len, "%s snoozes soundly at %02u:00...", pet->petName, (unsigned)local_hour);
        } else if (sp.id == 9) {
            snprintf(out, out_len, "%s surfaces for a calm breath and grazes %s.",
                     pet->petName, sp.primaryFood);
        } else if (r % 3 == 0) {
            snprintf(out, out_len, "%s %s! Remember when we %s?",
                     pet->petName, trait_adj, (mem && mem->lastAction[0]) ? mem->lastAction : "played");
        } else if (r % 3 == 1) {
            snprintf(out, out_len, "%s explores %s with %u%% energy!",
                     pet->petName, sp.biomeName, (unsigned)pet->energy);
        } else {
            snprintf(out, out_len, "%s", sp.funFacts[r % 3]);
        }
    }

    size_t max_w = screen_bubble ? LLM_MAX_TOK_SCREEN : LLM_MAX_TOK_WEB;
    size_t max_c = screen_bubble ? 110 : 220;
    safety_validate_output(out, max_w, max_c < out_len - 1 ? max_c : out_len - 1);
}

// =============================================================================
// Core 0 Worker Task
// =============================================================================
static void brain_worker_task(void* /*arg*/) {
    BrainRequest req;
    for (;;) {
        if (xQueueReceive(_req_queue, &req, portMAX_DELAY) == pdTRUE) {
            char reply[256] = {};
            pet_brain_generate_sync(&req.pet, &req.mem, req.local_hour, req.battery_pct,
                                    req.user_msg, req.screen_bubble,
                                    reply, sizeof(reply), req.cb, req.user_ctx);
        }
    }
}

void pet_brain_init() {
    _model_loaded = try_mount_zple_partition();
    if (!_req_queue) {
        _req_queue = xQueueCreate(4, sizeof(BrainRequest));
        xTaskCreatePinnedToCore(brain_worker_task, "pet_brain", 8192, nullptr, 1, nullptr, 0);
    }
}

BrainBackend pet_brain_backend() {
    return _model_loaded ? BrainBackend::ON_CHIP_PLE : BrainBackend::FALLBACK_RULES;
}

bool pet_brain_has_model() { return _model_loaded; }
bool pet_brain_is_busy()   { return _busy; }

bool pet_brain_ask_async(const ZooPetState* pet, const PetMemory* mem,
                         uint8_t local_hour, uint8_t battery_pct,
                         const char* user_msg, bool screen_bubble,
                         BrainTokenCb cb, void* user_ctx) {
    if (!_req_queue || !pet) return false;
    BrainRequest req = {};
    req.pet           = *pet;
    if (mem) req.mem  = *mem;
    req.local_hour    = local_hour;
    req.battery_pct   = battery_pct;
    req.screen_bubble = screen_bubble;
    req.cb            = cb;
    req.user_ctx      = user_ctx;
    strlcpy(req.user_msg, user_msg ? user_msg : "hello", sizeof(req.user_msg));
    return xQueueSend(_req_queue, &req, 0) == pdTRUE;
}

bool pet_brain_generate_sync(const ZooPetState* pet, const PetMemory* mem,
                             uint8_t local_hour, uint8_t battery_pct,
                             const char* user_msg, bool screen_bubble,
                             char* out_reply, size_t out_len,
                             BrainTokenCb stream_cb, void* user_ctx) {
    if (!pet || !out_reply || out_len < 16) return false;

    const SpeciesProfile& sp = species_get(pet->speciesId);
    SanitizeResult san = safety_sanitize_input(user_msg, pet->petName, sp.name);
    if (!san.safe) {
        strlcpy(out_reply, san.deflection, out_len);
        if (stream_cb) stream_cb(out_reply, true, user_ctx);
        return true;
    }

    RagHit rag_hit = {};
    bool has_rag = rag_is_ready() && rag_query(san.cleanInput, sp.slug, &rag_hit, 1) > 0 && rag_hit.score >= 1.2f;

    // Use real on-chip PLE inference if flashed AND battery >= 10%
    if (_model_loaded && battery_pct >= LLM_MIN_BATTERY_PCT) {
        _busy = true;
        sleep_boost_cpu(true);

        char ctx_hdr[128];
        memory_build_prompt_context(pet, mem, local_hour, ctx_hdr, sizeof(ctx_hdr));
        char full_prompt[256];
        if (has_rag) {
            snprintf(full_prompt, sizeof(full_prompt), "%s [RAG:%s] %s", ctx_hdr, rag_hit.tag, san.cleanInput);
        } else {
            snprintf(full_prompt, sizeof(full_prompt), "%s %s", ctx_hdr, san.cleanInput);
        }

        int pos = 0;
        ple_forward_step(1 /*<bos>*/, pos++, 0, nullptr);

        char pcopy[256];
        strlcpy(pcopy, full_prompt, sizeof(pcopy));
        char* sp_ctx = nullptr;
        char* w = strtok_r(pcopy, " ", &sp_ctx);
        while (w && pos < (_hdr.max_seq_len - 40)) {
            uint16_t tid = hash_word_to_vin(w, _hdr.v_in);
            ple_forward_step(tid, pos++, 0, nullptr);
            w = strtok_r(nullptr, " ", &sp_ctx);
        }

        size_t max_tok = screen_bubble ? LLM_MAX_TOK_SCREEN : LLM_MAX_TOK_WEB;
        out_reply[0] = '\0';
        size_t cur_len = 0;

        uint16_t cur_in = 1; // <bos> triggers first output token
        for (size_t step = 0; step < max_tok && pos < _hdr.max_seq_len; step++) {
            ple_forward_step(cur_in, pos++, _hdr.v_out, _logits);
            uint16_t next_out = sample_top_k(_logits, _hdr.v_out, 0.7f);
            if (next_out <= 2) break; // <pad>, <bos>, <eos>
            if (next_out >= ZOO_VOUT_COUNT) continue;

            const char* word = ZOO_VOUT_WORDS[next_out];
            cur_in = hash_word_to_vin(word, _hdr.v_in);
            bool is_punct = (word[0] == '.' || word[0] == ',' || word[0] == '!' || word[0] == '?' || word[0] == ':');
            size_t wl = strlen(word);
            if (cur_len + wl + 2 >= out_len) break;
            if (cur_len > 0 && !is_punct) out_reply[cur_len++] = ' ';
            memcpy(out_reply + cur_len, word, wl);
            cur_len += wl;
            out_reply[cur_len] = '\0';

            if (stream_cb) stream_cb(out_reply, false, user_ctx);
            if ((step & 3) == 3) vTaskDelay(1); // feed WDT cleanly
        }

        sleep_boost_cpu(false);
        _busy = false;

        if (safety_validate_output(out_reply, max_tok, out_len - 1)) {
            if (stream_cb) stream_cb(out_reply, true, user_ctx);
            return true;
        }
    }

    // Fallback: instant, rich species/trait/memory-aware composition (grounded with RAG when confident)
    generate_fallback_reply(pet, mem, local_hour, san.cleanInput, has_rag ? &rag_hit : nullptr,
                            screen_bubble, out_reply, out_len);
    if (stream_cb) stream_cb(out_reply, true, user_ctx);
    return true;
}
