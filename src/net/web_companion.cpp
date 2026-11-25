#include "web_companion.h"
#include "../config.h"
#include "../rtc/rtc_driver.h"
#include "../power/battery.h"
#include "../power/sleep_controller.h"
#include "../buzzer/buzzer.h"
#include "../ai/micro_llm_ple.h"
#include "../ai/rag_retriever.h"
#include "../learn/study_decks.h"
#include "../learn/study_engine.h"
#include "../health/health_tracker.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static AppConfig*   _cfg      = nullptr;
static ZooPetState* _pet      = nullptr;
static PetMemory*   _mem      = nullptr;
static void (*_ui_cb)(const char*) = nullptr;

static SemaphoreHandle_t _pet_mtx   = nullptr;
static httpd_handle_t    _httpd     = nullptr;
static DNSServer         _dns;
static bool              _active    = false;
static bool              _trying_sta = false;
static bool              _sta_ok    = false;
static bool              _ap_mode   = false;
static bool              _ntp_done  = false;
static uint32_t          _sta_start = 0;
static volatile bool     _dirty     = false;

void pet_state_lock() {
    if (!_pet_mtx) _pet_mtx = xSemaphoreCreateRecursiveMutex();
    xSemaphoreTakeRecursive(_pet_mtx, portMAX_DELAY);
}
void pet_state_unlock() {
    if (_pet_mtx) xSemaphoreGiveRecursive(_pet_mtx);
}
bool web_companion_consume_dirty() {
    bool d = _dirty;
    _dirty = false;
    return d;
}

static void json_escape(const char* src, char* dst, size_t max_len) {
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 2 < max_len; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            dst[j++] = '\\';
            dst[j++] = c;
        } else if ((unsigned char)c >= 0x20) {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

static esp_err_t send_json_file_or_empty(httpd_req_t* req, const char* path, const char* fallback_json) {
    httpd_resp_set_type(req, "application/json");
    if (!LittleFS.exists(path)) {
        return httpd_resp_sendstr(req, fallback_json);
    }
    File f = LittleFS.open(path, "r");
    if (!f) {
        return httpd_resp_sendstr(req, fallback_json);
    }
    char buf[512];
    while (f.available()) {
        size_t n = f.readBytes(buf, sizeof(buf));
        if (n > 0) httpd_resp_send_chunk(req, buf, n);
    }
    f.close();
    return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t handle_root(httpd_req_t* req) {
    if (LittleFS.exists("/web/index.html.gz")) {
        File f = LittleFS.open("/web/index.html.gz", "r");
        if (f) {
            httpd_resp_set_type(req, "text/html");
            httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
            char buf[1024];
            while (f.available()) {
                size_t n = f.readBytes(buf, sizeof(buf));
                if (n > 0) httpd_resp_send_chunk(req, buf, n);
            }
            f.close();
            return httpd_resp_send_chunk(req, nullptr, 0);
        }
    }
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req,
        "<!doctype html><html><body><h2>ZooGotchi S3</h2>"
        "<p>Upload LittleFS image (pio run -t uploadfs) for full Web UI.</p></body></html>");
}

static esp_err_t handle_state(httpd_req_t* req) {
    JsonDocument doc;
    pet_state_lock();
    const SpeciesProfile& sp = species_get(_pet->speciesId);
    PetMood mood = zoo_compute_mood(_pet, millis());
    uint8_t hr = rtc_get_hour(_cfg->tz_offset_min);
    uint8_t mn = rtc_get_minute(_cfg->tz_offset_min);

    doc["active"]       = _pet->hasActivePet;
    doc["dead"]         = _pet->isDead;
    doc["cause"]        = (uint8_t)_pet->deathCause;
    doc["speciesId"]    = _pet->speciesId;
    doc["speciesName"]  = sp.name;
    doc["scientific"]   = sp.scientificName;
    doc["category"]     = (uint8_t)_pet->category;
    doc["categoryName"] = category_name(_pet->category);
    doc["biome"]        = sp.biomeName;
    doc["circadian"]    = circadian_name(sp.circadian);
    doc["isKarimono"]   = sp.isKarimono;
    doc["petName"]      = _pet->petName;
    doc["keeperName"]   = _pet->keeperName;
    doc["stage"]        = _pet->stage;
    doc["stageName"]    = sp.stageNames[_pet->stage < LIFE_STAGE_COUNT ? _pet->stage : 0];
    doc["ageHours"]     = _pet->ageSeconds / 3600UL;
    doc["lifespanHours"]= sp.naturalLifespanHours;
    doc["primaryFood"]  = sp.primaryFood;
    doc["treatFood"]    = sp.treatFood;
    doc["envLabel"]     = sp.envCareLabel;

    doc["hunger"]       = _pet->hunger;
    doc["hydration"]    = _pet->hydration;
    doc["envCare"]      = _pet->envCare;
    doc["enrichment"]   = _pet->enrichment;
    doc["energy"]       = _pet->energy;
    doc["health"]       = _pet->health;
    doc["bond"]         = _pet->bond;

    doc["playfulCalm"]    = _pet->playfulCalm;
    doc["braveShy"]       = _pet->braveShy;
    doc["affectionIndep"] = _pet->affectionIndep;
    doc["curiousRoutine"] = _pet->curiousRoutine;

    doc["sick"]         = _pet->sick;
    doc["sleeping"]     = _pet->sleeping;
    doc["mood"]         = zoo_mood_name(mood);
    doc["coins"]        = _pet->zooCoins;
    doc["stepsToday"]   = _pet->stepsToday;
    doc["stepsTotal"]   = _pet->stepsTotal;
    doc["batteryPct"]   = battery_get_percent();
    doc["rtcHour"]      = hr;
    doc["rtcMin"]       = mn;
    doc["rtcEpoch"]     = (int64_t)rtc_get_epoch();
    doc["brainBackend"] = pet_brain_has_model() ? "On-Chip PLE Micro-LLM" : "On-Device Personality Engine";
    doc["ragReady"]     = rag_is_ready();
    doc["ragChunks"]    = rag_chunk_count();
    doc["funFact"]      = sp.funFacts[(_pet->ageSeconds / 60) % 3];

    if (sp.isKarimono) {
        char code[13];
        karimono_encode_code(_pet->genome, code);
        doc["generation"] = _pet->genome.generation;
        doc["genomeCode"] = code;
        doc["canBreed"]   = zoo_karimono_can_breed(_pet);
        doc["parentA"]    = _pet->genome.parentAName;
        doc["parentB"]    = _pet->genome.parentBName;
    }
    pet_state_unlock();

    String json;
    serializeJson(doc, json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), json.length());
}

static esp_err_t handle_diary(httpd_req_t* req) {
    return send_json_file_or_empty(req, "/memory.json", "{\"diary\":[]}");
}

static esp_err_t handle_lineage(httpd_req_t* req) {
    return send_json_file_or_empty(req, "/lineage.json", "{\"entries\":[]}");
}

static esp_err_t handle_memorial(httpd_req_t* req) {
    return send_json_file_or_empty(req, "/memorial.json", "{\"memorials\":[]}");
}

static bool read_req_body(httpd_req_t* req, char* buf, size_t max_len) {
    int total = req->content_len;
    if (total <= 0 || (size_t)total >= max_len) return false;
    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, total - got);
        if (r <= 0) return false;
        got += r;
    }
    buf[got] = '\0';
    return true;
}

static void sse_stream_cb(const char* partial, bool finished, void* ctx) {
    httpd_req_t* req = (httpd_req_t*)ctx;
    char esc[320];
    json_escape(partial, esc, sizeof(esc));
    char line[384];
    snprintf(line, sizeof(line), "data: {\"done\":%s,\"text\":\"%s\"}\n\n",
             finished ? "true" : "false", esc);
    httpd_resp_send_chunk(req, line, strlen(line));
}

static esp_err_t handle_chat(httpd_req_t* req) {
    char body[256];
    if (!read_req_body(req, body, sizeof(body))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
    }
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }
    const char* msg = doc["message"] | "hello";

    ZooPetState snap_pet;
    PetMemory   snap_mem;
    pet_state_lock();
    snap_pet = *_pet;
    snap_mem = *_mem;
    _mem->totalChats++;
    pet_state_unlock();

    uint8_t hr  = rtc_get_hour(_cfg->tz_offset_min);
    uint8_t bat = battery_get_percent();

    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    RagHit rhit = {};
    const SpeciesProfile& sp = species_get(snap_pet.speciesId);
    if (rag_is_ready() && rag_query(msg, sp.slug, &rhit, 1) > 0 && rhit.score >= 1.2f) {
        char tesc[96];
        json_escape(rhit.title, tesc, sizeof(tesc));
        char rline[160];
        snprintf(rline, sizeof(rline), "data: {\"rag\":\"%s\",\"score\":%.2f}\n\n",
                 tesc, (double)rhit.score);
        httpd_resp_send_chunk(req, rline, strlen(rline));
    }

    char reply[256] = {};
    pet_brain_generate_sync(&snap_pet, &snap_mem, hr, bat, msg, false,
                            reply, sizeof(reply), sse_stream_cb, req);
    httpd_resp_send_chunk(req, nullptr, 0);

    pet_state_lock();
    strlcpy(_mem->lastAction, "chatted together", sizeof(_mem->lastAction));
    memory_save(_mem);
    pet_state_unlock();

    if (_ui_cb) _ui_cb(reply);
    return ESP_OK;
}

static esp_err_t handle_action(httpd_req_t* req) {
    char body[128];
    if (!read_req_body(req, body, sizeof(body))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
    }
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }
    const char* act = doc["action"] | "";
    ZooAction za = ZooAction::PET_STROKE;
    if      (strcmp(act, "feed")  == 0) za = ZooAction::FEED_PRIMARY;
    else if (strcmp(act, "treat") == 0) za = ZooAction::FEED_TREAT;
    else if (strcmp(act, "env")   == 0) za = ZooAction::ENV_CARE;
    else if (strcmp(act, "vet")   == 0) za = ZooAction::VET_MEDICINE;
    else if (strcmp(act, "sleep") == 0) za = ZooAction::TOGGLE_SLEEP;

    char fb[64] = {};
    bool ok = false;
    pet_state_lock();
    ok = zoo_perform_action(_pet, za, fb, sizeof(fb));
    if (ok) {
        if (za == ZooAction::FEED_PRIMARY || za == ZooAction::FEED_TREAT) _mem->totalMeals++;
        if (za == ZooAction::PET_STROKE) { _mem->totalPets++; _pet->lastPetHeartMs = millis(); }
        strlcpy(_mem->lastAction, fb, sizeof(_mem->lastAction));
        memory_record_event(_mem, fb, rtc_get_epoch());
        zoo_save_state(_pet);
        _dirty = true;
    }
    pet_state_unlock();

    if (ok && _ui_cb) _ui_cb(fb);
    JsonDocument out;
    out["ok"]  = ok;
    out["msg"] = fb;
    String s; serializeJson(out, s);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, s.c_str(), s.length());
}

static esp_err_t handle_breed(httpd_req_t* req) {
    char body[192];
    if (!read_req_body(req, body, sizeof(body))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
    }
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }

    const char* code = doc["code"] | "";
    const char* pname = doc["partnerName"] | "Plaza Spirit";
    const char* cname = doc["childName"] | "";

    bool ok = false;
    char msg[64] = {};
    pet_state_lock();
    if (!zoo_karimono_can_breed(_pet)) {
        strlcpy(msg, "Requires Adult Karimono!", sizeof(msg));
    } else {
        KarimonoGenome partner;
        bool valid_partner = true;
        if (code[0]) {
            valid_partner = karimono_decode_code(code, &partner);
            if (!valid_partner) strlcpy(msg, "Invalid 12-char Genome Code", sizeof(msg));
        } else {
            partner = karimono_generate_plaza_partner(_pet->genome.generation, esp_random());
        }
        if (valid_partner) {
            ok = zoo_karimono_crossbreed(_pet, partner, pname, cname, rtc_get_epoch(), esp_random());
            if (ok) {
                snprintf(msg, sizeof(msg), "Hatched Gen %u: %s!", (unsigned)_pet->genome.generation, _pet->petName);
                memory_record_event(_mem, msg, rtc_get_epoch());
                zoo_save_state(_pet);
                _dirty = true;
            }
        }
    }
    pet_state_unlock();

    if (ok) {
        buzzer_play_async(MELODY_BREED, MELODY_BREED_LEN);
        if (_ui_cb) _ui_cb(msg);
    }
    JsonDocument out;
    out["ok"]  = ok;
    out["msg"] = msg;
    String s; serializeJson(out, s);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, s.c_str(), s.length());
}

static esp_err_t handle_config(httpd_req_t* req) {
    char body[320];
    if (!read_req_body(req, body, sizeof(body))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
    }
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }

    pet_state_lock();
    if (doc["keeperName"].is<const char*>()) {
        strlcpy(_pet->keeperName, doc["keeperName"], sizeof(_pet->keeperName));
    }
    if (doc["petName"].is<const char*>()) {
        strlcpy(_pet->petName, doc["petName"], sizeof(_pet->petName));
    }
    if (doc["wifiSsid"].is<const char*>()) {
        strlcpy(_cfg->wifi_ssid, doc["wifiSsid"], sizeof(_cfg->wifi_ssid));
    }
    if (doc["wifiPass"].is<const char*>()) {
        strlcpy(_cfg->wifi_password, doc["wifiPass"], sizeof(_cfg->wifi_password));
    }
    if (doc["tzOffsetMin"].is<int>()) {
        _cfg->tz_offset_min = (int16_t)doc["tzOffsetMin"].as<int>();
    }
    if (doc["demoSpeed"].is<bool>()) {
        _cfg->demo_speed = doc["demoSpeed"].as<bool>();
    }
    if (doc["buzzer"].is<bool>()) {
        _cfg->buzzer_enabled = doc["buzzer"].as<bool>();
        buzzer_set_muted(!_cfg->buzzer_enabled);
    }
    if (doc["rtcEpoch"].is<int64_t>()) {
        int64_t ep = doc["rtcEpoch"].as<int64_t>();
        if (ep > 1700000000LL) rtc_set_epoch((time_t)ep);
    }
    config_manager_save(_cfg);
    zoo_save_state(_pet);
    _dirty = true;
    pet_state_unlock();

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t handle_study(httpd_req_t* req) {
    JsonDocument doc;
    int64_t ep = (int64_t)rtc_get_epoch();
    const StudyStats& st = study_get_stats();
    doc["xp"]          = st.xpTotal;
    doc["streak"]      = st.studyStreakDays;
    doc["mastered"]    = study_mastered_count();
    doc["focusMin"]    = st.focusMinutesTotal;
    doc["totalDue"]    = study_total_due_count(ep);
    doc["answered"]    = st.totalAnswered;
    doc["correct"]     = st.totalCorrect;
    pet_state_lock();
    doc["lastLearned"] = _mem ? _mem->lastLearned : "";
    pet_state_unlock();

    JsonArray decks = doc["decks"].to<JsonArray>();
    for (uint8_t d = 0; d < (uint8_t)StudyDeckId::COUNT; d++) {
        StudyDeckId did = (StudyDeckId)d;
        JsonObject o = decks.add<JsonObject>();
        o["id"]    = d;
        o["name"]  = study_deck_name(did);
        o["total"] = study_deck_card_count(did);
        o["due"]   = study_due_count(did, ep);
    }

    String s;
    serializeJson(doc, s);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, s.c_str(), s.length());
}

static esp_err_t handle_custom_deck_get(httpd_req_t* req) {
    return send_json_file_or_empty(req, "/custom_deck.json", "{\"cards\":[]}");
}

static esp_err_t handle_custom_deck_post(httpd_req_t* req) {
    int total = req->content_len;
    if (total <= 0 || total > 24576) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Deck size must be 1..24576 bytes");
    }
    char* buf = (char*)heap_caps_malloc((size_t)total + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (char*)malloc((size_t)total + 1);
    if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");

    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, total - got);
        if (r <= 0) {
            free(buf);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Recv error");
        }
        got += r;
    }
    buf[got] = '\0';

    bool ok = study_custom_deck_save_json(buf, (size_t)got);
    free(buf);

    JsonDocument out;
    out["ok"]    = ok;
    out["count"] = study_deck_card_count(StudyDeckId::CUSTOM);
    String s; serializeJson(out, s);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, s.c_str(), s.length());
}

static esp_err_t handle_rag_query(httpd_req_t* req) {
    char body[192];
    if (!read_req_body(req, body, sizeof(body))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
    }
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }
    const char* q     = doc["query"] | "";
    const char* scope = doc["scope"] | "all";
    pet_state_lock();
    const char* slug = _pet ? species_get(_pet->speciesId).slug : "";
    pet_state_unlock();

    RagHit* hits = (RagHit*)calloc(7, sizeof(RagHit));
    if (!hits) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
    }
    uint8_t cnt = 0;
    if (rag_is_ready()) {
        if (strcmp(scope, "zoo") == 0) {
            cnt = q[0] ? rag_query_zoo(q, slug, hits, 5)
                       : rag_list_species_chunks(slug, hits, 7);
        } else {
            cnt = rag_query(q, slug, hits, 3);
        }
    }

    JsonDocument out;
    out["ready"]  = rag_is_ready();
    out["chunks"] = rag_chunk_count();
    out["terms"]  = rag_term_count();
    JsonArray arr = out["hits"].to<JsonArray>();
    for (uint8_t i = 0; i < cnt; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["title"]    = hits[i].title;
        o["tag"]      = hits[i].tag;
        o["category"] = hits[i].category;
        o["snippet"]  = hits[i].snippet;
        o["score"]    = hits[i].score;
    }
    free(hits);
    String s;
    serializeJson(out, s);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, s.c_str(), s.length());
}

static void fill_health_json(JsonDocument& out) {
    pet_state_lock();
    const HealthState& hs = health_get_state();
    uint32_t steps_day    = _pet ? _pet->stepsToday : 0;
    uint32_t steps_tot    = _pet ? _pet->stepsTotal : 0;
    uint32_t goal         = hs.stepGoal > 0 ? hs.stepGoal : 6000;
    uint32_t pct          = (steps_day * 100UL) / goal;
    if (pct > 100) pct = 100;

    out["stepsToday"]          = steps_day;
    out["stepsTotal"]          = steps_tot;
    out["stepGoal"]            = hs.stepGoal;
    out["goalPct"]             = pct;
    out["goalStreakDays"]      = hs.goalStreakDays;
    out["waterCupsToday"]      = hs.waterCupsToday;
    out["waterGoalCups"]       = hs.waterGoalCups;
    out["breatheSessions"]     = hs.breatheSessionsTotal;
    out["remindersEnabled"]    = hs.remindersEnabled;
    out["reminderIntervalMin"] = hs.reminderIntervalMin;
    out["weekAvg"]             = health_week_avg_steps(steps_day);
    JsonArray wk = out["weekSteps"].to<JsonArray>();
    for (int i = 0; i < 7; i++) wk.add(hs.weekSteps[i]);
    pet_state_unlock();
}

static esp_err_t handle_health_get(httpd_req_t* req) {
    JsonDocument out;
    fill_health_json(out);
    String s;
    serializeJson(out, s);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, s.c_str(), s.length());
}

static esp_err_t handle_health_post(httpd_req_t* req) {
    char body[256];
    if (!read_req_body(req, body, sizeof(body))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
    }
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }

    const char* act = doc["action"] | "";
    bool logged_water = (strcmp(act, "water") == 0);
    if (logged_water) {
        pet_state_lock();
        health_log_water(_pet, (int64_t)rtc_get_epoch());
        zoo_save_state(_pet);
        _dirty = true;
        pet_state_unlock();
        buzzer_play_async(MELODY_FEED, MELODY_FEED_LEN);
        if (_ui_cb) _ui_cb("Water logged! Pet hydrated +10%!");
    } else {
        pet_state_lock();
        const HealthState& cur = health_get_state();
        uint32_t sg   = doc["stepGoal"] | cur.stepGoal;
        uint8_t  wg   = (uint8_t)(doc["waterGoalCups"] | cur.waterGoalCups);
        bool     rem  = doc["remindersEnabled"] | cur.remindersEnabled;
        uint16_t rmin = (uint16_t)(doc["reminderIntervalMin"] | cur.reminderIntervalMin);
        health_set_config(sg, wg, rem, rmin);
        _dirty = true;
        pet_state_unlock();
    }

    JsonDocument out;
    out["ok"] = true;
    fill_health_json(out);
    String s;
    serializeJson(out, s);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, s.c_str(), s.length());
}

static void start_http_server() {
    if (_httpd) return;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 20;
    if (httpd_start(&_httpd, &config) != ESP_OK) return;

    const httpd_uri_t uris[] = {
        {"/",                 HTTP_GET,  handle_root,             nullptr},
        {"/api/state",        HTTP_GET,  handle_state,            nullptr},
        {"/api/diary",        HTTP_GET,  handle_diary,            nullptr},
        {"/api/lineage",      HTTP_GET,  handle_lineage,          nullptr},
        {"/api/memorial",     HTTP_GET,  handle_memorial,         nullptr},
        {"/api/chat",         HTTP_POST, handle_chat,             nullptr},
        {"/api/action",       HTTP_POST, handle_action,           nullptr},
        {"/api/breed",        HTTP_POST, handle_breed,            nullptr},
        {"/api/config",       HTTP_POST, handle_config,           nullptr},
        {"/api/study",        HTTP_GET,  handle_study,            nullptr},
        {"/api/rag",          HTTP_POST, handle_rag_query,        nullptr},
        {"/api/health",       HTTP_GET,  handle_health_get,       nullptr},
        {"/api/health",       HTTP_POST, handle_health_post,      nullptr},
        {"/api/decks/custom", HTTP_GET,  handle_custom_deck_get,  nullptr},
        {"/api/decks/custom", HTTP_POST, handle_custom_deck_post, nullptr},
    };
    for (const auto& u : uris) {
        httpd_register_uri_handler(_httpd, &u);
    }
    Serial.println("[net] HTTP server started");
}

static void begin_softap() {
    _trying_sta = false;
    _sta_ok     = false;
    _ap_mode    = true;
    WiFi.disconnect(false);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SOFTAP_SSID, WIFI_SOFTAP_PASS);
    _dns.start(53, "*", WiFi.softAPIP());
    MDNS.begin(WIFI_MDNS_HOST);
    start_http_server();
    Serial.printf("[net] SoftAP '%s' ready at http://%s\n",
                  WIFI_SOFTAP_SSID, WiFi.softAPIP().toString().c_str());
}

void web_companion_init(AppConfig* cfg, ZooPetState* pet, PetMemory* mem,
                        void (*on_remote_ui_cb)(const char* speech_msg)) {
    _cfg   = cfg;
    _pet   = pet;
    _mem   = mem;
    _ui_cb = on_remote_ui_cb;
    if (!_pet_mtx) _pet_mtx = xSemaphoreCreateRecursiveMutex();
    WiFi.mode(WIFI_OFF);
}

void web_companion_start() {
    if (_active) return;
    _active   = true;
    _sta_ok   = false;
    _ap_mode  = false;
    _ntp_done = false;
    sleep_boost_cpu(true);

    if (_cfg && _cfg->wifi_ssid[0]) {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(WIFI_PS_MIN_MODEM);
        WiFi.begin(_cfg->wifi_ssid, _cfg->wifi_password);
        _trying_sta = true;
        _sta_start  = millis();
        Serial.printf("[net] Joining STA '%s'...\n", _cfg->wifi_ssid);
    } else {
        begin_softap();
    }
    _dirty = true;
}

void web_companion_stop() {
    if (!_active) return;
    _active     = false;
    _trying_sta = false;
    _sta_ok     = false;
    _ap_mode    = false;

    if (_httpd) {
        httpd_stop(_httpd);
        _httpd = nullptr;
    }
    _dns.stop();
    MDNS.end();
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    sleep_boost_cpu(false);
    _dirty = true;
    Serial.println("[net] Companion Wi-Fi stopped");
}

void web_companion_toggle() {
    if (_active) web_companion_stop();
    else         web_companion_start();
}

void web_companion_update() {
    if (!_active) return;
    if (_ap_mode) {
        _dns.processNextRequest();
        return;
    }
    if (_trying_sta) {
        if (WiFi.status() == WL_CONNECTED) {
            _trying_sta = false;
            _sta_ok     = true;
            MDNS.begin(WIFI_MDNS_HOST);
            start_http_server();
            configTime(NTP_GMT_OFFSET, NTP_DST_OFFSET, NTP_SERVER1, NTP_SERVER2);
            _dirty = true;
            Serial.printf("[net] STA IP: http://%s\n", WiFi.localIP().toString().c_str());
        } else if ((millis() - _sta_start) >= WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("[net] STA timeout — falling back to SoftAP");
            begin_softap();
            _dirty = true;
        }
    } else if (_sta_ok && !_ntp_done) {
        time_t now = time(nullptr);
        if (now > 1700000000L) {
            _ntp_done = true;
            rtc_driver_sync_from_ntp(now);
        }
    }
}

bool web_companion_is_active()        { return _active; }
bool web_companion_is_sta_connected() { return _active && _sta_ok; }

void web_companion_get_url(char* out, size_t len) {
    if (!out || len == 0) return;
    if (!_active) { strlcpy(out, "Wi-Fi Off", len); return; }
    if (_ap_mode) {
        snprintf(out, len, "http://%s", WiFi.softAPIP().toString().c_str());
    } else if (_sta_ok) {
        snprintf(out, len, "http://%s", WiFi.localIP().toString().c_str());
    } else {
        strlcpy(out, "Connecting...", len);
    }
}

void web_companion_get_ssid(char* out, size_t len) {
    if (!out || len == 0) return;
    if (!_active) { strlcpy(out, "Off", len); return; }
    if (_ap_mode) strlcpy(out, WIFI_SOFTAP_SSID, len);
    else if (_sta_ok && _cfg) strlcpy(out, _cfg->wifi_ssid, len);
    else strlcpy(out, "Connecting", len);
}
