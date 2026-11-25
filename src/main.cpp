// =============================================================================
// ZooGotchi S3 — Main Application Entry Point
// Waveshare ESP32-S3-Touch-LCD-1.69 (ST7789V2, CST816T, QMI8658C, PCF85063A)
// =============================================================================
#include <Arduino.h>
#include <LittleFS.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "config_mgr/config_manager.h"
#include "hal/i2c_lock.h"
#include "display/lvgl_port.h"
#include "display/ui_screens.h"
#include "input/touch_cst816.h"
#include "input/buttons.h"
#include "buzzer/buzzer.h"
#include "imu/imu_driver.h"
#include "rtc/rtc_driver.h"
#include "power/battery.h"
#include "power/sleep_controller.h"
#include "zoo/species_registry.h"
#include "zoo/karimono_genetics.h"
#include "zoo/zoo_engine.h"
#include "game/minigames/forage_catch.h"
#include "game/minigames/habitat_care.h"
#include "game/minigames/rhythm_chirp.h"
#include "learn/study_decks.h"
#include "learn/study_engine.h"
#include "learn/focus_timer.h"
#include "health/health_tracker.h"
#include "ai/personality_memory.h"
#include "ai/rag_retriever.h"
#include "ai/micro_llm_ple.h"
#include "net/web_companion.h"

static AppConfig   g_cfg;
static ZooPetState g_pet;
static PetMemory   g_mem;
static uint8_t     g_bat_pct        = 100;
static int8_t      g_active_game    = -1;
static uint32_t    g_last_game_ms   = 0;
static uint32_t    g_last_imu_ms    = 0;
static uint32_t    g_last_save_s    = 0;
static uint32_t    g_last_bat_ms    = 0;
static bool        g_memorial_shown = false;

// Active quiz round state
static constexpr uint8_t QUIZ_ROUND_LEN = 5;
static StudyDeckId g_quiz_deck      = StudyDeckId::DSA_PATTERNS;
static uint8_t     g_quiz_q_num     = 0;
static uint8_t     g_quiz_card_idx  = 0;
static int16_t     g_quiz_prev_idx  = -1;
static StudyCard   g_quiz_card      = {};
static bool        g_quiz_answered  = false;
static uint32_t    g_quiz_adv_at_ms = 0;
static uint8_t     g_quiz_correct   = 0;
static uint16_t    g_quiz_coins     = 0;
static char        g_quiz_last_term[48] = {};

static volatile bool g_speech_pending = false;
static char          g_speech_buf[140] = {};

static void post_speech_from_any_core(const char* text) {
    if (!text || !text[0]) return;
    strlcpy(g_speech_buf, text, sizeof(g_speech_buf));
    g_speech_pending = true;
}

static void on_brain_reply(const char* partial, bool finished, void* /*ctx*/) {
    if (finished && partial && partial[0]) {
        post_speech_from_any_core(partial);
    }
}

static void ask_pet_brain(const char* prompt) {
    pet_state_lock();
    ZooPetState spet = g_pet;
    PetMemory   smem = g_mem;
    pet_state_unlock();

    uint8_t hr = rtc_get_hour(g_cfg.tz_offset_min);
    pet_brain_ask_async(&spet, &smem, hr, g_bat_pct, prompt, true, on_brain_reply, nullptr);
}

static void save_all_atomic() {
    pet_state_lock();
    zoo_save_state(&g_pet);
    memory_save(&g_mem);
    study_save();
    health_save();
    pet_state_unlock();
}

static void refresh_settings_ui() {
    char ssid[64] = {}, url[64] = {};
    web_companion_get_ssid(ssid, sizeof(ssid));
    web_companion_get_url(url, sizeof(url));
    ui_refresh_settings(&g_cfg, web_companion_is_active(), ssid, url);
}

// =============================================================================
// Quiz Helpers
// =============================================================================
static void quiz_load_next_question() {
    if (g_quiz_q_num >= QUIZ_ROUND_LEN) {
        pet_state_lock();
        zoo_apply_study_reward(&g_pet, g_quiz_coins, (uint8_t)(12 + g_quiz_correct * 4),
                               (uint8_t)(2 + g_quiz_correct), 3);
        if (g_quiz_last_term[0]) {
            strlcpy(g_mem.lastLearned, g_quiz_last_term, sizeof(g_mem.lastLearned));
        }
        char ev[64];
        snprintf(ev, sizeof(ev), "Quiz %s: %u/%u (+%uc)",
                 study_deck_name(g_quiz_deck), (unsigned)g_quiz_correct,
                 (unsigned)QUIZ_ROUND_LEN, (unsigned)g_quiz_coins);
        strlcpy(g_mem.lastAction, "studied flashcards", sizeof(g_mem.lastAction));
        memory_record_event(&g_mem, ev, rtc_get_epoch());
        zoo_save_state(&g_pet);
        pet_state_unlock();

        buzzer_play_async(MELODY_HAPPY, MELODY_HAPPY_LEN);
        ui_show_screen(UiScreen::HOME);
        ui_show_speech_bubble(ev, 5000);
        return;
    }

    int64_t ep = (int64_t)rtc_get_epoch();
    uint8_t cnt = study_deck_card_count(g_quiz_deck);
    for (int tries = 0; tries < 4; tries++) {
        if (!study_pick_next_card(g_quiz_deck, ep, &g_quiz_card_idx)) break;
        if (cnt <= 1 || (int16_t)g_quiz_card_idx != g_quiz_prev_idx) break;
    }
    g_quiz_prev_idx = g_quiz_card_idx;
    if (!study_deck_get_card(g_quiz_deck, g_quiz_card_idx, &g_quiz_card)) {
        ui_show_screen(UiScreen::STUDY_HUB);
        return;
    }
    g_quiz_q_num++;
    g_quiz_answered  = false;
    g_quiz_adv_at_ms = 0;
    ui_show_quiz_card(study_deck_name(g_quiz_deck), g_quiz_q_num, QUIZ_ROUND_LEN, &g_quiz_card);
}

// =============================================================================
// UI Callbacks
// =============================================================================
static void cb_adopt_species(uint8_t species_id) {
    int64_t now_ep = (int64_t)rtc_get_epoch();
    pet_state_lock();
    zoo_adopt_species(&g_pet, species_id, nullptr, now_ep);
    g_memorial_shown = false;
    const SpeciesProfile& sp = species_get(species_id);
    char ev[64];
    snprintf(ev, sizeof(ev), "Welcomed %s!", sp.name);
    strlcpy(g_mem.lastAction, "hatched", sizeof(g_mem.lastAction));
    memory_record_event(&g_mem, ev, now_ep);
    zoo_save_state(&g_pet);
    pet_state_unlock();

    buzzer_play_async(MELODY_EVOLVE, MELODY_EVOLVE_LEN);
    ui_show_screen(UiScreen::HOME);
    ask_pet_brain("hello");
}

static void cb_zoo_action(ZooAction act) {
    char fb[64] = {};
    bool ok = false;
    pet_state_lock();
    ok = zoo_perform_action(&g_pet, act, fb, sizeof(fb));
    if (ok) {
        if (act == ZooAction::FEED_PRIMARY || act == ZooAction::FEED_TREAT) g_mem.totalMeals++;
        if (act == ZooAction::PET_STROKE) { g_mem.totalPets++; g_pet.lastPetHeartMs = millis(); }
        strlcpy(g_mem.lastAction, fb, sizeof(g_mem.lastAction));
        memory_record_event(&g_mem, fb, rtc_get_epoch());
        zoo_save_state(&g_pet);
    }
    AnimalCategory cat = g_pet.category;
    pet_state_unlock();

    if (ok) {
        if (act == ZooAction::VET_MEDICINE) buzzer_play_async(MELODY_MEDICINE, MELODY_MEDICINE_LEN);
        else if (act == ZooAction::PET_STROKE) buzzer_play_category_emote((uint8_t)cat);
        else buzzer_play_async(MELODY_FEED, MELODY_FEED_LEN);
        ui_show_speech_bubble(fb, 3000);
        if (act == ZooAction::FEED_PRIMARY || act == ZooAction::PET_STROKE) {
            ask_pet_brain(act == ZooAction::PET_STROKE ? "pet" : "eat");
        }
    } else if (fb[0]) {
        ui_show_speech_bubble(fb, 2500);
    }
}

static uint8_t     g_journal_rag_idx = 0;
static uint8_t     g_talk_turn       = 0;

static void cb_ask_pet_talk() {
    char q[96] = {};
    AnimalCategory cat = AnimalCategory::LAND;
    pet_state_lock();
    cat = g_pet.category;
    const SpeciesProfile& sp = species_get(g_pet.speciesId);
    if (g_pet.sick) {
        snprintf(q, sizeof(q), "%s sick vet medicine tonic cure", sp.slug);
    } else if (g_pet.hunger < 45) {
        snprintf(q, sizeof(q), "%s %s care food", sp.slug, sp.primaryFood);
    } else if (g_pet.envCare < 45) {
        snprintf(q, sizeof(q), "%s %s care biome", sp.slug, sp.envCareLabel);
    } else {
        uint8_t slot = g_talk_turn++ & 0x03;
        if (slot == 0)      strlcpy(q, "How are you feeling?", sizeof(q));
        else if (slot == 1) snprintf(q, sizeof(q), "%s biology fun facts", sp.slug);
        else if (slot == 2) snprintf(q, sizeof(q), "%s lifecycle stages evolve", sp.slug);
        else                snprintf(q, sizeof(q), "%s %s care", sp.slug, sp.biomeName);
    }
    pet_state_unlock();
    buzzer_play_category_emote((uint8_t)cat);
    ask_pet_brain(q);
}

static void cb_journal_zoo_guide() {
    if (!rag_is_ready()) {
        ui_journal_show_rag_card("Stats View", "On-device Zoo RAG index (/rag/kb.bin) not mounted.");
        return;
    }

    pet_state_lock();
    const SpeciesProfile& sp = species_get(g_pet.speciesId);
    char ctx_q[96] = {};
    if (g_pet.sick) {
        snprintf(ctx_q, sizeof(ctx_q), "sick vet medicine tonic cure");
    } else if (g_pet.hunger < 45) {
        snprintf(ctx_q, sizeof(ctx_q), "%s %s care food", sp.slug, sp.primaryFood);
    } else if (g_pet.hydration < 45) {
        snprintf(ctx_q, sizeof(ctx_q), "%s water hydration care", sp.slug);
    } else if (g_pet.envCare < 45) {
        snprintf(ctx_q, sizeof(ctx_q), "%s %s care biome", sp.slug, sp.envCareLabel);
    } else if (sp.isKarimono) {
        snprintf(ctx_q, sizeof(ctx_q), "%s karimono cross-breeding genome mutation", sp.slug);
    } else {
        snprintf(ctx_q, sizeof(ctx_q), "%s biome guide circadian personality", sp.slug);
    }
    pet_state_unlock();

    RagHit sp_hits[3] = {};
    uint8_t sp_cnt = rag_list_species_chunks(sp.slug, sp_hits, 3);
    RagHit ctx_hit = {};
    bool has_ctx = (rag_query_zoo(ctx_q, sp.slug, &ctx_hit, 1) > 0);
    uint8_t total = (uint8_t)(sp_cnt + (has_ctx ? 1 : 0));

    if (total == 0 || g_journal_rag_idx >= total) {
        g_journal_rag_idx = 0;
        ui_journal_reset_view();
        uint8_t hr = 0, mn = 0;
        rtc_epoch_to_local_hm(rtc_get_epoch(), g_cfg.tz_offset_min, &hr, &mn);
        pet_state_lock();
        ui_update_home(&g_pet, &g_cfg, hr, mn, g_bat_pct, web_companion_is_active(), millis());
        pet_state_unlock();
        return;
    }

    const RagHit& h = (g_journal_rag_idx < sp_cnt) ? sp_hits[g_journal_rag_idx] : ctx_hit;
    char body[240];
    snprintf(body, sizeof(body), "%s (%u/%u)\n\n%s",
             h.title, (unsigned)(g_journal_rag_idx + 1), (unsigned)total, h.snippet);
    g_journal_rag_idx++;
    const char* btn_lbl = (g_journal_rag_idx < total) ? "Next Tip" : "Stats View";
    ui_journal_show_rag_card(btn_lbl, body);
}

static void cb_buy_use_shop(uint8_t item_idx) {
    char msg[64] = {};
    bool ok = false;
    pet_state_lock();
    ok = zoo_shop_buy_or_use(&g_pet, item_idx, msg, sizeof(msg));
    if (ok) zoo_save_state(&g_pet);
    pet_state_unlock();

    buzzer_play_async(ok ? MELODY_HAPPY : MELODY_SICK, ok ? MELODY_HAPPY_LEN : 1);
    ui_show_screen(UiScreen::HOME);
    ui_show_speech_bubble(msg, 3500);
}

static void cb_breed_plaza() {
    pet_state_lock();
    if (!zoo_karimono_can_breed(&g_pet)) {
        bool is_k = species_get(g_pet.speciesId).isKarimono;
        pet_state_unlock();
        if (!is_k) {
            ui_show_screen(UiScreen::ADOPT_CAT);
        } else {
            ui_show_screen(UiScreen::HOME);
            ui_show_speech_bubble("Raise Karimono to Adult first!", 3000);
        }
        return;
    }
    KarimonoGenome partner = karimono_generate_plaza_partner(g_pet.genome.generation, esp_random());
    bool ok = zoo_karimono_crossbreed(&g_pet, partner, "Plaza Spirit", nullptr,
                                      (int64_t)rtc_get_epoch(), esp_random());
    char msg[64] = {};
    if (ok) {
        snprintf(msg, sizeof(msg), "Hatched Gen %u: %s!", (unsigned)g_pet.genome.generation, g_pet.petName);
        memory_record_event(&g_mem, msg, rtc_get_epoch());
        zoo_save_state(&g_pet);
    }
    pet_state_unlock();

    if (ok) {
        buzzer_play_async(MELODY_BREED, MELODY_BREED_LEN);
        ui_show_screen(UiScreen::HOME);
        ui_show_speech_bubble(msg, 5000);
    }
}

static void cb_start_minigame(uint8_t game_idx) {
    pet_state_lock();
    const SpeciesProfile& sp = species_get(g_pet.speciesId);
    pet_state_unlock();

    sleep_boost_cpu(true);
    g_active_game = (int8_t)game_idx;
    ui_show_screen(UiScreen::MINIGAME);
    lv_obj_t* root = ui_minigame_parent();

    if (game_idx == 0) forage_catch_start(root, sp.biomeBgHex, sp.biomeAccentHex, sp.primaryFood);
    else if (game_idx == 1) habitat_care_start(root, sp.biomeBgHex, sp.biomeAccentHex, sp.envCareLabel);
    else rhythm_chirp_start(root, sp.biomeBgHex);
}

static void stop_active_minigame(const MiniGameResult* res) {
    if (g_active_game < 0) return;
    forage_catch_stop();
    habitat_care_stop();
    rhythm_chirp_stop();
    g_active_game = -1;
    sleep_boost_cpu(false);

    if (res && res->finished) {
        pet_state_lock();
        zoo_apply_minigame_result(&g_pet, res->score, res->coinsEarned, res->braveryDelta);
        g_mem.totalGames++;
        strlcpy(g_mem.lastAction, "played a mini-game", sizeof(g_mem.lastAction));
        memory_record_event(&g_mem, res->summary, rtc_get_epoch());
        zoo_save_state(&g_pet);
        pet_state_unlock();

        buzzer_play_async(MELODY_HAPPY, MELODY_HAPPY_LEN);
        ui_show_screen(UiScreen::HOME);
        ui_show_speech_bubble(res->summary, 4000);
    } else {
        ui_show_screen(UiScreen::HOME);
    }
}

static void cb_toggle_wifi() {
    web_companion_toggle();
    refresh_settings_ui();
}

static void cb_toggle_mute() {
    g_cfg.buzzer_enabled = !g_cfg.buzzer_enabled;
    buzzer_set_muted(!g_cfg.buzzer_enabled);
    config_manager_save(&g_cfg);
    refresh_settings_ui();
}

static void cb_toggle_demo() {
    g_cfg.demo_speed = !g_cfg.demo_speed;
    config_manager_save(&g_cfg);
    refresh_settings_ui();
}

static void cb_toggle_bgr() {
    g_cfg.bgr_order = !g_cfg.bgr_order;
    gfx.setBgrOrder(g_cfg.bgr_order);
    config_manager_save(&g_cfg);
    refresh_settings_ui();
}

static void cb_adjust_clock_hour(int8_t delta_hr) {
    time_t cur = rtc_get_epoch();
    rtc_set_epoch(cur + (time_t)delta_hr * 3600L);
}

static void cb_open_study_hub() {
    ui_refresh_study_hub((int64_t)rtc_get_epoch());
    ui_show_screen(UiScreen::STUDY_HUB);
}

static void cb_start_quiz_deck(uint8_t deck_id) {
    if (deck_id >= (uint8_t)StudyDeckId::COUNT) return;
    StudyDeckId did = (StudyDeckId)deck_id;
    if (study_deck_card_count(did) == 0) {
        ui_show_screen(UiScreen::HOME);
        ui_show_speech_bubble("Upload Custom Deck in Wi-Fi Study tab!", 4000);
        return;
    }
    g_quiz_deck         = did;
    g_quiz_q_num        = 0;
    g_quiz_prev_idx     = -1;
    g_quiz_correct      = 0;
    g_quiz_coins        = 0;
    g_quiz_last_term[0] = '\0';
    ui_show_screen(UiScreen::QUIZ);
    quiz_load_next_question();
}

static void cb_quiz_answer(uint8_t choice_idx) {
    if (g_quiz_answered) return;
    g_quiz_answered = true;
    bool ok = (choice_idx == g_quiz_card.correctIdx);
    if (ok) {
        g_quiz_correct++;
        strlcpy(g_quiz_last_term, g_quiz_card.termTag ? g_quiz_card.termTag : "", sizeof(g_quiz_last_term));
        buzzer_play_async(MELODY_FEED, MELODY_FEED_LEN);
    } else {
        buzzer_play_async(MELODY_SICK, 1);
    }
    g_quiz_coins += study_record_answer(g_quiz_deck, g_quiz_card_idx, ok, (int64_t)rtc_get_epoch());
    ui_show_quiz_feedback(choice_idx, g_quiz_card.correctIdx, g_quiz_card.takeaway);
    g_quiz_adv_at_ms = millis() + 2400UL;
}

static void cb_quiz_next() {
    if (!g_quiz_answered) return;
    quiz_load_next_question();
}

static void cb_open_focus() {
    uint32_t now = millis();
    pet_state_lock();
    char pname[16];
    strlcpy(pname, g_pet.petName, sizeof(pname));
    pet_state_unlock();
    ui_refresh_focus(focus_timer_is_active(), focus_timer_duration_min(),
                     focus_timer_remaining_sec(now), focus_timer_progress_pct(now),
                     g_cfg.demo_speed, pname);
    ui_show_screen(UiScreen::FOCUS);
}

static void cb_start_focus(uint16_t minutes) {
    uint32_t now = millis();
    focus_timer_start(minutes, now);
    buzzer_play_async(MELODY_BOOT, 2);
    cb_open_focus();
}

static void cb_stop_focus() {
    focus_timer_stop();
    cb_open_focus();
}

static void refresh_health_ui() {
    uint32_t now = millis();
    BreatheSnapshot br = health_breathe_snapshot(now);
    pet_state_lock();
    ui_refresh_health(&g_pet, &health_get_state(), &br);
    pet_state_unlock();
}

static void cb_open_health() {
    refresh_health_ui();
    ui_show_screen(UiScreen::HEALTH);
}

static void cb_log_water() {
    pet_state_lock();
    health_log_water(&g_pet, (int64_t)rtc_get_epoch());
    zoo_save_state(&g_pet);
    pet_state_unlock();
    buzzer_play_async(MELODY_FEED, MELODY_FEED_LEN);
    refresh_health_ui();
}

static void cb_adjust_step_goal(int32_t delta) {
    pet_state_lock();
    health_adjust_step_goal(delta);
    pet_state_unlock();
    refresh_health_ui();
}

static void cb_toggle_health_reminders() {
    pet_state_lock();
    health_toggle_reminders();
    pet_state_unlock();
    refresh_health_ui();
}

static void cb_toggle_breathe() {
    uint32_t now = millis();
    if (health_breathe_is_active()) {
        health_breathe_stop();
    } else {
        health_breathe_start(now);
        buzzer_play_async(MELODY_LULLABY, 2);
    }
    refresh_health_ui();
}

// =============================================================================
// setup() & loop()
// =============================================================================
void setup() {
    pinMode(PIN_SYS_EN, OUTPUT);
    digitalWrite(PIN_SYS_EN, HIGH);
    pinMode(PIN_SYS_OUT, INPUT);

    Serial.begin(115200);
    { uint32_t t = millis(); while (!Serial && (millis() - t) < 1200) delay(10); }
    Serial.println("\n=== ZooGotchi S3 Boot ===");

    config_manager_load(&g_cfg);
    config_manager_print(&g_cfg);

    i2c_bus_init();
    lvgl_port_init(g_cfg.bgr_order);
    lvgl_port_set_brightness(g_cfg.brightness);

    touch_cst816_init();
    touch_cst816_register_lvgl();
    buttons_init();

    buzzer_init();
    buzzer_set_muted(!g_cfg.buzzer_enabled);
    if (g_cfg.flash_alerts) buzzer_set_play_cb([]() { display_alert_flash(2); });

    imu_driver_init();
    rtc_driver_init();
    battery_init();
    sleep_controller_init(g_cfg.brightness);
    study_init();

    int64_t boot_ep = (int64_t)rtc_get_epoch();
    health_init(boot_ep, g_cfg.tz_offset_min);

    float bat_v = battery_read_voltage();
    g_bat_pct   = battery_v_to_percent(bat_v);

    pet_state_lock();
    bool has_save = zoo_load_state(&g_pet);
    memory_load(&g_mem);
    if (has_save && g_pet.hasActivePet && !g_pet.isDead) {
        if (health_check_midnight_rollover(&g_pet, boot_ep, g_cfg.tz_offset_min)) {
            zoo_save_state(&g_pet);
        }
        ZooTickEvents cev = zoo_catchup_elapsed(&g_pet, boot_ep,
                                                g_cfg.tz_offset_min, g_cfg.demo_speed);
        if (cev.died) {
            g_memorial_shown = false;
        }
        zoo_save_state(&g_pet);
    }
    pet_state_unlock();

    UiCallbacks cbs = {
        cb_adopt_species, cb_zoo_action, cb_ask_pet_talk, cb_buy_use_shop,
        cb_breed_plaza, cb_start_minigame, cb_toggle_wifi, cb_toggle_mute,
        cb_toggle_demo, cb_toggle_bgr, cb_adjust_clock_hour,
        cb_open_study_hub, cb_start_quiz_deck, cb_quiz_answer, cb_quiz_next,
        cb_open_focus, cb_start_focus, cb_stop_focus,
        cb_open_health, cb_log_water, cb_adjust_step_goal,
        cb_toggle_health_reminders, cb_toggle_breathe,
        cb_journal_zoo_guide
    };
    ui_screens_init(cbs);

    if (!has_save || !g_pet.hasActivePet) {
        ui_show_screen(UiScreen::ADOPT_CAT);
    } else if (g_pet.isDead) {
        ui_refresh_memorial(&g_pet);
        ui_show_screen(UiScreen::MEMORIAL);
        g_memorial_shown = true;
    } else {
        ui_show_screen(UiScreen::HOME);
    }

    rag_init();
    pet_brain_init();
    web_companion_init(&g_cfg, &g_pet, &g_mem, post_speech_from_any_core);

    buzzer_play_async(MELODY_BOOT, MELODY_BOOT_LEN);
    g_last_game_ms = millis();
    g_last_imu_ms  = millis();
    g_last_bat_ms  = millis();
}

void loop() {
    uint32_t now = millis();

    touch_cst816_update();
    buttons_update();
    buzzer_update();
    web_companion_update();
    lvgl_port_tick();

    if (g_speech_pending) {
        g_speech_pending = false;
        if (ui_current_screen() == UiScreen::HOME) {
            ui_show_speech_bubble(g_speech_buf, 6500);
        }
    }

    if (web_companion_consume_dirty()) {
        refresh_settings_ui();
    }

    // Quiz feedback auto-advance
    if (ui_current_screen() == UiScreen::QUIZ && g_quiz_answered &&
        g_quiz_adv_at_ms > 0 && now >= g_quiz_adv_at_ms) {
        g_quiz_adv_at_ms = 0;
        quiz_load_next_question();
    }

    // Focus Study Timer completion check
    if (focus_timer_check_complete(now)) {
        sleep_controller_mark_activity();
        uint16_t mins  = focus_timer_duration_min();
        uint16_t coins = study_record_focus_session(mins, (int64_t)rtc_get_epoch());
        pet_state_lock();
        zoo_apply_study_reward(&g_pet, coins, 20, 6, 4);
        char ev[64];
        snprintf(ev, sizeof(ev), "Focus %um done! (+%uc)", (unsigned)mins, (unsigned)coins);
        strlcpy(g_mem.lastAction, "finished study sprint", sizeof(g_mem.lastAction));
        memory_record_event(&g_mem, ev, rtc_get_epoch());
        zoo_save_state(&g_pet);
        pet_state_unlock();

        buzzer_play_async(MELODY_EVOLVE, MELODY_EVOLVE_LEN);
        ui_show_screen(UiScreen::HOME);
        ui_show_speech_bubble(ev, 5500);
    }

    // Guided 4-4-4-4 Breathing Session completion check
    if (health_breathe_check_complete(now)) {
        sleep_controller_mark_activity();
        pet_state_lock();
        zoo_apply_study_reward(&g_pet, 5, 8, 4, 0);
        if (g_pet.playfulCalm > -98) g_pet.playfulCalm -= 2;
        else                         g_pet.playfulCalm = -100;
        strlcpy(g_mem.lastAction, "practiced calm breathing", sizeof(g_mem.lastAction));
        memory_record_event(&g_mem, "1m box breathing complete (+5c)", rtc_get_epoch());
        zoo_save_state(&g_pet);
        pet_state_unlock();

        buzzer_play_async(MELODY_PURR, MELODY_PURR_LEN);
        refresh_health_ui();
        if (ui_current_screen() != UiScreen::HEALTH) {
            ui_show_screen(UiScreen::HOME);
            ui_show_speech_bubble("1m breathing complete! Calm & refreshed.", 4500);
        }
    }

    TouchEvent tev = touch_cst816_get_event();
    if (tev.type != TouchEventType::NONE) {
        sleep_controller_mark_activity();
        if (g_active_game < 0 && ui_current_screen() == UiScreen::HOME) {
            if (tev.type == TouchEventType::PET_STROKE) {
                cb_zoo_action(ZooAction::PET_STROKE);
            } else if (tev.type == TouchEventType::SWIPE_LEFT) {
                g_journal_rag_idx = 0;
                ui_show_screen(UiScreen::JOURNAL);
            } else if (tev.type == TouchEventType::SWIPE_RIGHT) {
                ui_show_screen(UiScreen::CARE);
            } else if (tev.type == TouchEventType::SWIPE_UP) {
                cb_open_study_hub();
            } else if (tev.type == TouchEventType::SWIPE_DOWN) {
                refresh_settings_ui();
                ui_show_screen(UiScreen::SETTINGS);
            }
        } else if (g_active_game >= 0 && tev.type == TouchEventType::SWIPE_DOWN) {
            stop_active_minigame(nullptr);
        }
    }

    ButtonEvent bev = buttons_get_event();
    if (bev != ButtonEvent::NONE) {
        sleep_controller_mark_activity();
        switch (bev) {
        case ButtonEvent::PWR_SHORT:
            sleep_controller_toggle_screen_sleep();
            break;
        case ButtonEvent::PWR_DOUBLE:
            cb_toggle_wifi();
            refresh_settings_ui();
            if (g_active_game < 0) ui_show_screen(UiScreen::SETTINGS);
            break;
        case ButtonEvent::PWR_LONG:
            sleep_controller_power_off(save_all_atomic);
            break;
        case ButtonEvent::BOOT_SHORT:
            if (g_active_game >= 0) stop_active_minigame(nullptr);
            else if (g_pet.hasActivePet && !g_pet.isDead) ui_show_screen(UiScreen::HOME);
            break;
        default: break;
        }
    }

    if ((now - g_last_imu_ms) >= 50) {
        g_last_imu_ms = now;
        imu_driver_update();

        if (imu_consume_motion_wake() && sleep_controller_mode() != PowerMode::LIGHT_SLEEP) {
            sleep_controller_mark_activity();
        }

        uint32_t steps = imu_consume_new_steps();
        if (steps > 0) {
            sleep_controller_defer_deep_sleep();
            int64_t ep_now = (int64_t)rtc_get_epoch();
            bool goal_hit = false;
            pet_state_lock();
            zoo_apply_steps(&g_pet, steps);
            goal_hit = health_on_steps(&g_pet, steps, ep_now);
            if (goal_hit) {
                zoo_apply_study_reward(&g_pet, 25, 15, 15, 0);
                char ev[64];
                snprintf(ev, sizeof(ev), "Hit %lu step goal! (%ud streak)",
                         (unsigned long)health_get_state().stepGoal,
                         (unsigned)health_get_state().goalStreakDays);
                memory_record_event(&g_mem, ev, ep_now);
                zoo_save_state(&g_pet);
            }
            pet_state_unlock();
            if (goal_hit) {
                sleep_controller_mark_activity();
                buzzer_play_async(MELODY_EVOLVE, MELODY_EVOLVE_LEN);
                if (ui_current_screen() == UiScreen::HOME) {
                    ui_show_speech_bubble("Daily step goal reached! +25c!", 5500);
                } else if (ui_current_screen() == UiScreen::HEALTH) {
                    refresh_health_ui();
                }
            }
        }

        ShakeLevel sh = imu_get_shake();
        if (sh != ShakeLevel::NONE && g_active_game < 0 && !focus_timer_is_active()) {
            sleep_controller_mark_activity();
            bool hard = (sh == ShakeLevel::HARD);
            pet_state_lock();
            zoo_trigger_shake(&g_pet, hard, now);
            pet_state_unlock();
            if (hard) {
                buzzer_play_async(MELODY_DIZZY, MELODY_DIZZY_LEN);
                if (ui_current_screen() == UiScreen::HOME) ui_show_speech_bubble("Whoa... dizzy!", 2600);
            } else {
                buzzer_play_async(MELODY_HAPPY, 2);
            }
        }

        static uint32_t last_lullaby_ms = 0;
        if (imu_is_rocking_lullaby() && (now - last_lullaby_ms) >= 5000 && g_active_game < 0) {
            last_lullaby_ms = now;
            pet_state_lock();
            zoo_perform_action(&g_pet, ZooAction::ROCK_LULLABY, nullptr, 0);
            pet_state_unlock();
            buzzer_play_async(MELODY_LULLABY, MELODY_LULLABY_LEN);
            if (ui_current_screen() == UiScreen::HOME) ui_show_speech_bubble("Soothed by gentle rocking...", 3000);
        }
    }

    if (g_active_game >= 0) {
        MiniGameResult res = {};
        if (g_active_game == 0) {
            int16_t tx = 0, ty = 0;
            touch_cst816_get_xy(&tx, &ty);
            res = forage_catch_update(imu_get_tilt_x(), tx, touch_cst816_is_pressed(), now);
        } else if (g_active_game == 1) {
            res = habitat_care_update(now);
        } else {
            res = rhythm_chirp_update(now);
        }
        if (res.finished) {
            stop_active_minigame(&res);
        }
    }

    if ((now - g_last_bat_ms) >= 15000UL) {
        g_last_bat_ms = now;
        float v = battery_read_voltage();
        g_bat_pct = battery_v_to_percent(v);
        if (battery_is_low_brownout(v)) {
            sleep_controller_power_off(save_all_atomic);
        }
    }

    if ((now - g_last_game_ms) >= GAME_TICK_MS) {
        g_last_game_ms = now;

        int64_t ep = (int64_t)rtc_get_epoch();
        uint8_t hr = 0, mn = 0;
        rtc_epoch_to_local_hm((time_t)ep, g_cfg.tz_offset_min, &hr, &mn);

        ZooTickEvents tev_1s = {};
        char nudge_msg[64] = {};
        bool show_nudge = false;
        pet_state_lock();
        if (health_check_midnight_rollover(&g_pet, ep, g_cfg.tz_offset_min)) {
            zoo_save_state(&g_pet);
        }
        tev_1s = zoo_tick_1s(&g_pet, ep, hr, g_cfg.demo_speed, now);

        bool busy_or_asleep = (g_active_game >= 0) || focus_timer_is_active() ||
                              health_breathe_is_active() || g_pet.sleeping || g_pet.isDead ||
                              (ui_current_screen() != UiScreen::HOME);
        show_nudge = health_check_nudge(ep, hr, busy_or_asleep, nudge_msg, sizeof(nudge_msg));

        if (tev_1s.stageAdvanced) {
            const SpeciesProfile& sp = species_get(g_pet.speciesId);
            char ev[64];
            snprintf(ev, sizeof(ev), "Grew into %s!", sp.stageNames[tev_1s.newStage]);
            memory_record_event(&g_mem, ev, ep);
            zoo_save_state(&g_pet);
        }

        if (g_pet.isDead && !g_memorial_shown) {
            g_memorial_shown = true;
            zoo_save_state(&g_pet);
            ui_refresh_memorial(&g_pet);
            pet_state_unlock();
            buzzer_play_async(MELODY_DEAD, MELODY_DEAD_LEN);
            ui_show_screen(UiScreen::MEMORIAL);
        } else {
            if (g_active_game < 0) {
                if (ui_current_screen() == UiScreen::FOCUS && focus_timer_is_active()) {
                    ui_refresh_focus(true, focus_timer_duration_min(),
                                     focus_timer_remaining_sec(now),
                                     focus_timer_progress_pct(now),
                                     g_cfg.demo_speed, g_pet.petName);
                } else if (ui_current_screen() == UiScreen::HEALTH) {
                    BreatheSnapshot br = health_breathe_snapshot(now);
                    ui_refresh_health(&g_pet, &health_get_state(), &br);
                } else {
                    ui_update_home(&g_pet, &g_cfg, hr, mn, g_bat_pct,
                                   web_companion_is_active(), now);
                }
            }
            pet_state_unlock();

            if (tev_1s.stageAdvanced) {
                buzzer_play_async(MELODY_EVOLVE, MELODY_EVOLVE_LEN);
                ui_show_speech_bubble("Evolved to a new stage!", 4500);
            } else if (tev_1s.becameSick) {
                buzzer_play_async(MELODY_SICK, MELODY_SICK_LEN);
            } else if (show_nudge) {
                buzzer_play_async(MELODY_PURR, 2);
                ui_show_speech_bubble(nudge_msg, 5500);
            }
        }

        if ((now / 1000UL) - g_last_save_s >= AUTOSAVE_INTERVAL_S) {
            g_last_save_s = now / 1000UL;
            save_all_atomic();
        }
    }

    // Keep device awake (only dim/light sleep allowed if not active) during Focus sprint, Breathing, or Quiz
    bool keep_awake = web_companion_is_active() || pet_brain_is_busy() ||
                      (g_active_game >= 0) || focus_timer_is_active() ||
                      health_breathe_is_active() || (ui_current_screen() == UiScreen::QUIZ);
    sleep_controller_update(keep_awake, pet_brain_is_busy(), (g_active_game >= 0), save_all_atomic);

    delay(5);
}
