#include "ui_screens.h"
#include "../config.h"
#include "../sprites/zoo_renderer.h"
#include "../learn/study_engine.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

static UiCallbacks _cbs = {};
static UiScreen    _cur = UiScreen::HOME;

static lv_obj_t* _scr_adopt_cat  = nullptr;
static lv_obj_t* _scr_adopt_sp   = nullptr;
static lv_obj_t* _scr_home       = nullptr;
static lv_obj_t* _scr_care       = nullptr;
static lv_obj_t* _scr_play       = nullptr;
static lv_obj_t* _scr_shop       = nullptr;
static lv_obj_t* _scr_journal    = nullptr;
static lv_obj_t* _scr_memorial   = nullptr;
static lv_obj_t* _scr_settings   = nullptr;
static lv_obj_t* _scr_minigame   = nullptr;
static lv_obj_t* _scr_study_hub  = nullptr;
static lv_obj_t* _scr_quiz       = nullptr;
static lv_obj_t* _scr_focus      = nullptr;

// Adopt species widgets
static AnimalCategory _sel_cat = AnimalCategory::LAND;
static lv_obj_t* _lbl_sp_title = nullptr;
static lv_obj_t* _sp_btn[3]    = {};
static lv_obj_t* _sp_lbl[3]    = {};
static uint8_t   _sp_ids[3]    = {0, 1, 2};

// Home widgets
static lv_obj_t* _lbl_clock    = nullptr;
static lv_obj_t* _lbl_title    = nullptr;
static lv_obj_t* _lbl_status_r = nullptr;
static lv_obj_t* _pet_canvas   = nullptr;
static lv_obj_t* _lbl_stage    = nullptr;
static lv_obj_t* _bar_v[4]     = {};
static lv_obj_t* _lbl_coins    = nullptr;
static lv_obj_t* _bubble_box   = nullptr;
static lv_obj_t* _bubble_lbl   = nullptr;
static uint32_t  _bubble_until = 0;
static uint8_t   _anim_frame   = 0;
static uint32_t  _last_anim_ms = 0;

// Care labels
static lv_obj_t* _care_lbl[5]  = {};

// Shop labels
static lv_obj_t* _shop_coins   = nullptr;
static lv_obj_t* _shop_btn_lbl[SHOP_ITEM_COUNT] = {};

// Journal & Memorial widgets
static lv_obj_t* _j_body       = nullptr;
static lv_obj_t* _j_breed_btn  = nullptr;
static lv_obj_t* _j_breed_lbl  = nullptr;
static lv_obj_t* _m_body       = nullptr;

// Settings widgets
static lv_obj_t* _s_wifi_lbl   = nullptr;
static lv_obj_t* _s_url_lbl    = nullptr;
static lv_obj_t* _s_qr         = nullptr;
static lv_obj_t* _s_mute_lbl   = nullptr;
static lv_obj_t* _s_demo_lbl   = nullptr;
static lv_obj_t* _s_bgr_lbl    = nullptr;

// Study Hub widgets
static lv_obj_t* _st_summary   = nullptr;
static lv_obj_t* _st_deck_lbl[(uint8_t)StudyDeckId::COUNT] = {};

// Quiz widgets
static lv_obj_t* _qz_hdr       = nullptr;
static lv_obj_t* _qz_prompt    = nullptr;
static lv_obj_t* _qz_btn[4]    = {};
static lv_obj_t* _qz_btn_lbl[4]= {};
static lv_obj_t* _qz_fb_box    = nullptr;
static lv_obj_t* _qz_fb_lbl    = nullptr;

// Focus widgets
static lv_obj_t* _fc_idle_box  = nullptr;
static lv_obj_t* _fc_demo_btn  = nullptr;
static lv_obj_t* _fc_run_box   = nullptr;
static lv_obj_t* _fc_arc       = nullptr;
static lv_obj_t* _fc_time_lbl  = nullptr;
static lv_obj_t* _fc_buddy_lbl = nullptr;

static lv_obj_t* make_screen(uint32_t bg_hex) {
    lv_obj_t* s = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

static lv_obj_t* make_btn(lv_obj_t* parent, int x, int y, int w, int h,
                          uint32_t bg_hex, uint32_t fg_hex, const char* txt,
                          lv_event_cb_t cb, void* user_data, lv_obj_t** out_lbl = nullptr) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 2, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_color(l, lv_color_hex(fg_hex), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    if (out_lbl) *out_lbl = l;
    return b;
}

static void add_header(lv_obj_t* scr, const char* title, lv_event_cb_t back_cb) {
    lv_obj_t* lbl = lv_label_create(scr);
    lv_obj_set_pos(lbl, 24, 14);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x1A3327), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl, title);
    if (back_cb) {
        make_btn(scr, 162, 10, 56, 26, 0xE2E8F0, 0x1E293B, "Back", back_cb, nullptr);
    }
}

static void go_home_cb(lv_event_t*) { ui_show_screen(UiScreen::HOME); }

// =============================================================================
// 1. Adoption Category & Species Screens
// =============================================================================
static void populate_adopt_species(AnimalCategory cat) {
    _sel_cat = cat;
    lv_label_set_text_fmt(_lbl_sp_title, "%s (Pick 1)", category_name(cat));
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t sid = species_id_for_cat(cat, i);
        _sp_ids[i] = sid;
        const SpeciesProfile& sp = species_get(sid);
        lv_obj_set_style_bg_color(_sp_btn[i], lv_color_hex(sp.biomeBgHex), 0);
        if (sp.isKarimono) {
            lv_label_set_text_fmt(_sp_lbl[i], "%s\n%s · Cross-Breed Lineage",
                                  sp.name, circadian_name(sp.circadian));
        } else {
            lv_label_set_text_fmt(_sp_lbl[i], "%s\n%s · %s",
                                  sp.name, circadian_name(sp.circadian), sp.primaryFood);
        }
    }
}

static void cat_pick_cb(lv_event_t* e) {
    AnimalCategory cat = (AnimalCategory)(uintptr_t)lv_event_get_user_data(e);
    populate_adopt_species(cat);
    ui_show_screen(UiScreen::ADOPT_SPECIES);
}

static void sp_pick_cb(lv_event_t* e) {
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx < 3 && _cbs.onAdoptSpecies) {
        _cbs.onAdoptSpecies(_sp_ids[idx]);
    }
}

static void build_adopt_screens() {
    _scr_adopt_cat = make_screen(0xFBF8F1);
    add_header(_scr_adopt_cat, "Choose Zoo Category", nullptr);

    static const uint32_t CAT_BG[6] = {0xE8F3E4, 0xE3EFFB, 0xF8F2DC, 0xDDF2F6, 0xF2EDE0, 0xEFE7FB};
    for (uint8_t i = 0; i < 6; i++) {
        int col = i % 2;
        int row = i / 2;
        int x = 18 + col * 104;
        int y = 46 + row * 68;
        make_btn(_scr_adopt_cat, x, y, 98, 58, CAT_BG[i], 0x1A3327,
                 category_name((AnimalCategory)i), cat_pick_cb, (void*)(uintptr_t)i);
    }

    _scr_adopt_sp = make_screen(0xFBF8F1);
    _lbl_sp_title = lv_label_create(_scr_adopt_sp);
    lv_obj_set_pos(_lbl_sp_title, 24, 14);
    lv_obj_set_style_text_color(_lbl_sp_title, lv_color_hex(0x1A3327), 0);
    lv_obj_set_style_text_font(_lbl_sp_title, &lv_font_montserrat_14, 0);
    lv_label_set_text(_lbl_sp_title, "Pick Species");
    make_btn(_scr_adopt_sp, 162, 10, 56, 26, 0xE2E8F0, 0x1E293B, "Back",
             [](lv_event_t*) { ui_show_screen(UiScreen::ADOPT_CAT); }, nullptr);

    for (uint8_t i = 0; i < 3; i++) {
        _sp_btn[i] = make_btn(_scr_adopt_sp, 16, 48 + i * 72, 208, 62, 0xFFFFFF, 0x1E293B,
                              "Species", sp_pick_cb, (void*)(uintptr_t)i, &_sp_lbl[i]);
        lv_label_set_long_mode(_sp_lbl[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_width(_sp_lbl[i], 192);
    }
}

// =============================================================================
// 2. Main Home Screen (Care · Study · Play · Bio dock + top-right Menu pill)
// =============================================================================
static void dock_care_cb(lv_event_t*)    { ui_show_screen(UiScreen::CARE); }
static void dock_study_cb(lv_event_t*)   { if (_cbs.onOpenStudyHub) _cbs.onOpenStudyHub(); else ui_show_screen(UiScreen::STUDY_HUB); }
static void dock_play_cb(lv_event_t*)    { ui_show_screen(UiScreen::PLAY); }
static void dock_journal_cb(lv_event_t*) { ui_show_screen(UiScreen::JOURNAL); }
static void top_menu_cb(lv_event_t*)     { ui_show_screen(UiScreen::SETTINGS); }
static void bubble_tap_cb(lv_event_t*)   { ui_hide_speech_bubble(); }
static void pet_tap_cb(lv_event_t*)      { if (_cbs.onAskPetTalk) _cbs.onAskPetTalk(); }

static void build_home_screen() {
    _scr_home = make_screen(0xFBF8F1);

    _lbl_clock = lv_label_create(_scr_home);
    lv_obj_set_pos(_lbl_clock, 22, 10);
    lv_obj_set_style_text_color(_lbl_clock, lv_color_hex(0x1A3327), 0);
    lv_obj_set_style_text_font(_lbl_clock, &lv_font_montserrat_12, 0);
    lv_label_set_text(_lbl_clock, "12:00");

    _lbl_title = lv_label_create(_scr_home);
    lv_obj_set_pos(_lbl_title, 62, 10);
    lv_obj_set_width(_lbl_title, 92);
    lv_label_set_long_mode(_lbl_title, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(_lbl_title, lv_color_hex(0x2E6F4E), 0);
    lv_obj_set_style_text_font(_lbl_title, &lv_font_montserrat_12, 0);
    lv_label_set_text(_lbl_title, "ZooGotchi");

    // Top-right battery/Wi-Fi pill opens Settings/Menu on tap
    lv_obj_t* menu_pill = make_btn(_scr_home, 156, 6, 66, 22, 0xE2E8F0, 0x1E293B, "100%",
                                   top_menu_cb, nullptr, &_lbl_status_r);
    lv_obj_set_style_radius(menu_pill, 10, 0);
    lv_obj_set_style_border_width(menu_pill, 0, 0);

    _pet_canvas = zoo_renderer_create_canvas(_scr_home);
    lv_obj_set_pos(_pet_canvas, 56, 36);
    lv_obj_add_flag(_pet_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_pet_canvas, pet_tap_cb, LV_EVENT_CLICKED, nullptr);

    _lbl_stage = lv_label_create(_scr_home);
    lv_obj_set_pos(_lbl_stage, 16, 168);
    lv_obj_set_width(_lbl_stage, 208);
    lv_obj_set_style_text_align(_lbl_stage, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(_lbl_stage, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_text_font(_lbl_stage, &lv_font_montserrat_12, 0);
    lv_label_set_text(_lbl_stage, "Stage · Mood");

    static const uint32_t VCOL[4] = {0xEA580C, 0x0284C7, 0x16A34A, 0x9333EA};
    for (int i = 0; i < 4; i++) {
        _bar_v[i] = lv_bar_create(_scr_home);
        lv_obj_set_pos(_bar_v[i], 20 + i * 51, 188);
        lv_obj_set_size(_bar_v[i], 46, 8);
        lv_bar_set_range(_bar_v[i], 0, 100);
        lv_obj_set_style_bg_color(_bar_v[i], lv_color_hex(0xE2E8F0), LV_PART_MAIN);
        lv_obj_set_style_bg_color(_bar_v[i], lv_color_hex(VCOL[i]), LV_PART_INDICATOR);
    }

    _lbl_coins = lv_label_create(_scr_home);
    lv_obj_set_pos(_lbl_coins, 18, 202);
    lv_obj_set_width(_lbl_coins, 204);
    lv_obj_set_style_text_align(_lbl_coins, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(_lbl_coins, lv_color_hex(0x475569), 0);
    lv_obj_set_style_text_font(_lbl_coins, &lv_font_montserrat_10, 0);
    lv_label_set_text(_lbl_coins, "40c · 0 steps · Tap pet to talk");

    // Bottom 4-button dock (inset from bottom rounded corners)
    make_btn(_scr_home, 18,  226, 48, 36, 0x2E6F4E, 0xFFFFFF, "Care",  dock_care_cb,    nullptr);
    make_btn(_scr_home, 70,  226, 48, 36, 0x7C3AED, 0xFFFFFF, "Study", dock_study_cb,   nullptr);
    make_btn(_scr_home, 122, 226, 48, 36, 0x0284C7, 0xFFFFFF, "Play",  dock_play_cb,    nullptr);
    make_btn(_scr_home, 174, 226, 48, 36, 0xD97706, 0xFFFFFF, "Bio",   dock_journal_cb, nullptr);

    _bubble_box = lv_obj_create(_scr_home);
    lv_obj_set_pos(_bubble_box, 14, 118);
    lv_obj_set_size(_bubble_box, 212, 74);
    lv_obj_set_style_bg_color(_bubble_box, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(_bubble_box, LV_OPA_90, 0);
    lv_obj_set_style_radius(_bubble_box, 14, 0);
    lv_obj_set_style_border_width(_bubble_box, 2, 0);
    lv_obj_set_style_border_color(_bubble_box, lv_color_hex(0x2E6F4E), 0);
    lv_obj_set_style_pad_all(_bubble_box, 6, 0);
    lv_obj_remove_flag(_bubble_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_bubble_box, bubble_tap_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(_bubble_box, LV_OBJ_FLAG_HIDDEN);

    _bubble_lbl = lv_label_create(_bubble_box);
    lv_obj_set_width(_bubble_lbl, 196);
    lv_label_set_long_mode(_bubble_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(_bubble_lbl, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_text_font(_bubble_lbl, &lv_font_montserrat_12, 0);
    lv_label_set_text(_bubble_lbl, "");
}

// =============================================================================
// 3. Care, Play, Shop, Journal, Memorial, Settings, MiniGame
// =============================================================================
static void care_act_cb(lv_event_t* e) {
    ZooAction a = (ZooAction)(uintptr_t)lv_event_get_user_data(e);
    ui_show_screen(UiScreen::HOME);
    if (_cbs.onZooAction) _cbs.onZooAction(a);
}

static void build_care_screen() {
    _scr_care = make_screen(0xFBF8F1);
    add_header(_scr_care, "Animal Care", go_home_cb);

    static const ZooAction ACTS[5] = {
        ZooAction::FEED_PRIMARY, ZooAction::FEED_TREAT,
        ZooAction::ENV_CARE, ZooAction::VET_MEDICINE, ZooAction::TOGGLE_SLEEP
    };
    static const uint32_t COLS[5] = {0xE8F5E9, 0xFEF3C7, 0xE0F2FE, 0xFCE7F3, 0xE2E8F0};
    for (int i = 0; i < 5; i++) {
        make_btn(_scr_care, 20, 44 + i * 44, 200, 38, COLS[i], 0x1E293B,
                 "Care", care_act_cb, (void*)(uintptr_t)ACTS[i], &_care_lbl[i]);
    }
}

static void play_game_cb(lv_event_t* e) {
    uint8_t gi = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (_cbs.onStartMiniGame) _cbs.onStartMiniGame(gi);
}

static void build_play_screen() {
    _scr_play = make_screen(0xFBF8F1);
    add_header(_scr_play, "Play & Shop", go_home_cb);
    make_btn(_scr_play, 20, 44,  200, 40, 0xFEF3C7, 0x1E293B, "Forage Catch (IMU Tilt)", play_game_cb, (void*)0);
    make_btn(_scr_play, 20, 90,  200, 40, 0xE0F2FE, 0x1E293B, "Habitat Care (Tap)",      play_game_cb, (void*)1);
    make_btn(_scr_play, 20, 136, 200, 40, 0xFCE7F3, 0x1E293B, "Rhythm Chirp (Sound)",    play_game_cb, (void*)2);
    make_btn(_scr_play, 20, 182, 200, 40, 0xE8F5E9, 0x1E293B, "Zoo Shop & Glasses",
             [](lv_event_t*) { ui_show_screen(UiScreen::SHOP); }, nullptr);
    make_btn(_scr_play, 20, 228, 200, 36, 0xE2E8F0, 0x1E293B, "Wi-Fi & Menu Settings",
             [](lv_event_t*) { ui_show_screen(UiScreen::SETTINGS); }, nullptr);
}

static void shop_item_cb(lv_event_t* e) {
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (_cbs.onBuyUseShop) _cbs.onBuyUseShop(idx);
}

static void build_shop_screen() {
    _scr_shop = make_screen(0xFBF8F1);
    add_header(_scr_shop, "Zoo Shop", [](lv_event_t*) { ui_show_screen(UiScreen::PLAY); });

    _shop_coins = lv_label_create(_scr_shop);
    lv_obj_set_pos(_shop_coins, 24, 38);
    lv_obj_set_style_text_color(_shop_coins, lv_color_hex(0xB45309), 0);
    lv_obj_set_style_text_font(_shop_coins, &lv_font_montserrat_12, 0);
    lv_label_set_text(_shop_coins, "Coins: 0");

    lv_obj_t* list = lv_obj_create(_scr_shop);
    lv_obj_set_pos(list, 14, 56);
    lv_obj_set_size(list, 212, 206);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 2, 0);

    for (uint8_t i = 0; i < SHOP_ITEM_COUNT; i++) {
        const ShopItem& it = shop_get_item(i);
        make_btn(list, 2, i * 44, 198, 38, 0xFFFFFF, 0x1E293B,
                 it.name, shop_item_cb, (void*)(uintptr_t)i, &_shop_btn_lbl[i]);
    }
}

static void build_journal_screen() {
    _scr_journal = make_screen(0xFBF8F1);
    add_header(_scr_journal, "Field Journal", go_home_cb);

    _j_body = lv_label_create(_scr_journal);
    lv_obj_set_pos(_j_body, 20, 44);
    lv_obj_set_width(_j_body, 200);
    lv_label_set_long_mode(_j_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(_j_body, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_text_font(_j_body, &lv_font_montserrat_12, 0);
    lv_label_set_text(_j_body, "Loading...");

    _j_breed_btn = make_btn(_scr_journal, 20, 224, 200, 38, 0x7C3AED, 0xFFFFFF,
                            "Cross-Breed", [](lv_event_t*) {
        if (_cbs.onBreedPlaza) _cbs.onBreedPlaza();
    }, nullptr, &_j_breed_lbl);
}

static void build_memorial_screen() {
    _scr_memorial = make_screen(0xF3F4F6);
    add_header(_scr_memorial, "Sanctuary Memorial", nullptr);

    _m_body = lv_label_create(_scr_memorial);
    lv_obj_set_pos(_m_body, 20, 52);
    lv_obj_set_width(_m_body, 200);
    lv_label_set_long_mode(_m_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(_m_body, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_text_font(_m_body, &lv_font_montserrat_12, 0);
    lv_label_set_text(_m_body, "");

    make_btn(_scr_memorial, 24, 220, 192, 40, 0x2E6F4E, 0xFFFFFF, "Adopt New Friend",
             [](lv_event_t*) { ui_show_screen(UiScreen::ADOPT_CAT); }, nullptr);
}

static void build_settings_screen() {
    _scr_settings = make_screen(0xFBF8F1);
    add_header(_scr_settings, "Menu & Wi-Fi", go_home_cb);

    make_btn(_scr_settings, 18, 42, 204, 34, 0x0284C7, 0xFFFFFF, "Wi-Fi Phone Chat: OFF",
             [](lv_event_t*) { if (_cbs.onToggleWifi) _cbs.onToggleWifi(); }, nullptr, &_s_wifi_lbl);

    _s_url_lbl = lv_label_create(_scr_settings);
    lv_obj_set_pos(_s_url_lbl, 20, 78);
    lv_obj_set_width(_s_url_lbl, 200);
    lv_obj_set_style_text_color(_s_url_lbl, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_text_font(_s_url_lbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(_s_url_lbl, "Toggle Wi-Fi to chat/upload decks");

    _s_qr = lv_qrcode_create(_scr_settings);
    lv_qrcode_set_size(_s_qr, 74);
    lv_qrcode_set_dark_color(_s_qr, lv_color_hex(0x1A3327));
    lv_qrcode_set_light_color(_s_qr, lv_color_hex(0xFFFFFF));
    lv_obj_set_pos(_s_qr, 83, 98);
    lv_obj_add_flag(_s_qr, LV_OBJ_FLAG_HIDDEN);

    make_btn(_scr_settings, 18,  178, 98, 34, 0xFFFFFF, 0x1E293B, "Sound: ON",
             [](lv_event_t*) { if (_cbs.onToggleMute) _cbs.onToggleMute(); }, nullptr, &_s_mute_lbl);
    make_btn(_scr_settings, 124, 178, 98, 34, 0xFFFFFF, 0x1E293B, "Demo: OFF",
             [](lv_event_t*) { if (_cbs.onToggleDemo) _cbs.onToggleDemo(); }, nullptr, &_s_demo_lbl);

    make_btn(_scr_settings, 18,  218, 64, 34, 0xFFFFFF, 0x1E293B, "-1 Hr",
             [](lv_event_t*) { if (_cbs.onAdjustClockHour) _cbs.onAdjustClockHour(-1); }, nullptr);
    make_btn(_scr_settings, 88,  218, 64, 34, 0xFFFFFF, 0x1E293B, "+1 Hr",
             [](lv_event_t*) { if (_cbs.onAdjustClockHour) _cbs.onAdjustClockHour(1); }, nullptr);
    make_btn(_scr_settings, 158, 218, 64, 34, 0xE2E8F0, 0x1E293B, "BGR",
             [](lv_event_t*) { if (_cbs.onToggleBgr) _cbs.onToggleBgr(); }, nullptr, &_s_bgr_lbl);
}

static void build_minigame_screen() {
    _scr_minigame = make_screen(0xFBF8F1);
}

// =============================================================================
// 4. Study Hub, Quiz & Focus Timer Screens
// =============================================================================
static void deck_pick_cb(lv_event_t* e) {
    uint8_t d = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (_cbs.onStartQuizDeck) _cbs.onStartQuizDeck(d);
}

static void build_study_hub_screen() {
    _scr_study_hub = make_screen(0xFBF8F1);
    add_header(_scr_study_hub, "Study Academy", go_home_cb);

    _st_summary = lv_label_create(_scr_study_hub);
    lv_obj_set_pos(_st_summary, 20, 38);
    lv_obj_set_width(_st_summary, 200);
    lv_obj_set_style_text_color(_st_summary, lv_color_hex(0x4C1D95), 0);
    lv_obj_set_style_text_font(_st_summary, &lv_font_montserrat_10, 0);
    lv_label_set_text(_st_summary, "XP: 0 · Streak: 0d · Due: 0");

    lv_obj_t* list = lv_obj_create(_scr_study_hub);
    lv_obj_set_pos(list, 14, 54);
    lv_obj_set_size(list, 212, 210);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 2, 0);

    make_btn(list, 2, 0, 198, 36, 0xEDE9FE, 0x4C1D95, "Focus Study Timer",
             [](lv_event_t*) { if (_cbs.onOpenFocus) _cbs.onOpenFocus(); }, nullptr);

    static const uint32_t DECK_BG[6] = {0xDBEAFE, 0xE0E7FF, 0xFEF3C7, 0xFCE7F3, 0xDCFCE7, 0xF1F5F9};
    for (uint8_t d = 0; d < (uint8_t)StudyDeckId::COUNT; d++) {
        make_btn(list, 2, 42 + d * 42, 198, 36, DECK_BG[d], 0x1E293B,
                 study_deck_name((StudyDeckId)d), deck_pick_cb, (void*)(uintptr_t)d, &_st_deck_lbl[d]);
    }
}

static void quiz_choice_cb(lv_event_t* e) {
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (_cbs.onQuizAnswer) _cbs.onQuizAnswer(idx);
}

static void quiz_next_cb(lv_event_t*) {
    if (_cbs.onQuizNext) _cbs.onQuizNext();
}

static void build_quiz_screen() {
    _scr_quiz = make_screen(0xFBF8F1);

    _qz_hdr = lv_label_create(_scr_quiz);
    lv_obj_set_pos(_qz_hdr, 22, 12);
    lv_obj_set_width(_qz_hdr, 140);
    lv_label_set_long_mode(_qz_hdr, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(_qz_hdr, lv_color_hex(0x4C1D95), 0);
    lv_obj_set_style_text_font(_qz_hdr, &lv_font_montserrat_12, 0);
    lv_label_set_text(_qz_hdr, "Quiz 1/5");

    make_btn(_scr_quiz, 166, 8, 54, 24, 0xE2E8F0, 0x1E293B, "Exit",
             [](lv_event_t*) { ui_show_screen(UiScreen::STUDY_HUB); }, nullptr);

    lv_obj_t* qcard = lv_obj_create(_scr_quiz);
    lv_obj_set_pos(qcard, 14, 36);
    lv_obj_set_size(qcard, 212, 66);
    lv_obj_set_style_bg_color(qcard, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(qcard, 12, 0);
    lv_obj_set_style_border_width(qcard, 1, 0);
    lv_obj_set_style_border_color(qcard, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_pad_all(qcard, 6, 0);
    lv_obj_remove_flag(qcard, LV_OBJ_FLAG_SCROLLABLE);

    _qz_prompt = lv_label_create(qcard);
    lv_obj_set_width(_qz_prompt, 198);
    lv_label_set_long_mode(_qz_prompt, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(_qz_prompt, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_text_font(_qz_prompt, &lv_font_montserrat_12, 0);
    lv_label_set_text(_qz_prompt, "");

    for (uint8_t i = 0; i < 4; i++) {
        _qz_btn[i] = make_btn(_scr_quiz, 16, 108 + i * 38, 208, 34, 0xFFFFFF, 0x1E293B,
                              "Choice", quiz_choice_cb, (void*)(uintptr_t)i, &_qz_btn_lbl[i]);
    }

    // Takeaway / Next feedback bar overlaid on bottom of prompt when answered
    _qz_fb_box = lv_obj_create(_scr_quiz);
    lv_obj_set_pos(_qz_fb_box, 14, 36);
    lv_obj_set_size(_qz_fb_box, 212, 66);
    lv_obj_set_style_bg_color(_qz_fb_box, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_radius(_qz_fb_box, 12, 0);
    lv_obj_set_style_pad_all(_qz_fb_box, 6, 0);
    lv_obj_remove_flag(_qz_fb_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_qz_fb_box, quiz_next_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(_qz_fb_box, LV_OBJ_FLAG_HIDDEN);

    _qz_fb_lbl = lv_label_create(_qz_fb_box);
    lv_obj_set_width(_qz_fb_lbl, 198);
    lv_label_set_long_mode(_qz_fb_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(_qz_fb_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(_qz_fb_lbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(_qz_fb_lbl, "");
}

static void focus_start_cb(lv_event_t* e) {
    uint16_t mins = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    if (_cbs.onStartFocus) _cbs.onStartFocus(mins);
}

static void build_focus_screen() {
    _scr_focus = make_screen(0xFBF8F1);
    add_header(_scr_focus, "Focus Study", [](lv_event_t*) {
        if (_cbs.onOpenStudyHub) _cbs.onOpenStudyHub();
        else ui_show_screen(UiScreen::STUDY_HUB);
    });

    _fc_idle_box = lv_obj_create(_scr_focus);
    lv_obj_set_pos(_fc_idle_box, 14, 44);
    lv_obj_set_size(_fc_idle_box, 212, 220);
    lv_obj_set_style_bg_opa(_fc_idle_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_fc_idle_box, 0, 0);
    lv_obj_set_style_pad_all(_fc_idle_box, 0, 0);
    lv_obj_remove_flag(_fc_idle_box, LV_OBJ_FLAG_SCROLLABLE);

    make_btn(_fc_idle_box, 6, 4,   200, 42, 0xE0F2FE, 0x1E293B, "15m Quick Review",   focus_start_cb, (void*)15);
    make_btn(_fc_idle_box, 6, 54,  200, 42, 0xEDE9FE, 0x4C1D95, "25m LeetCode Sprint", focus_start_cb, (void*)25);
    make_btn(_fc_idle_box, 6, 104, 200, 42, 0xDCFCE7, 0x14532D, "35m Mock Interview",  focus_start_cb, (void*)35);
    _fc_demo_btn = make_btn(_fc_idle_box, 6, 154, 200, 38, 0xFEF3C7, 0x78350F,
                            "1m Demo Sprint", focus_start_cb, (void*)1);

    _fc_run_box = lv_obj_create(_scr_focus);
    lv_obj_set_pos(_fc_run_box, 14, 44);
    lv_obj_set_size(_fc_run_box, 212, 224);
    lv_obj_set_style_bg_opa(_fc_run_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_fc_run_box, 0, 0);
    lv_obj_set_style_pad_all(_fc_run_box, 0, 0);
    lv_obj_remove_flag(_fc_run_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_fc_run_box, LV_OBJ_FLAG_HIDDEN);

    _fc_arc = lv_arc_create(_fc_run_box);
    lv_obj_set_size(_fc_arc, 126, 126);
    lv_obj_set_pos(_fc_arc, 43, 6);
    lv_arc_set_rotation(_fc_arc, 270);
    lv_arc_set_bg_angles(_fc_arc, 0, 360);
    lv_arc_set_range(_fc_arc, 0, 100);
    lv_arc_set_value(_fc_arc, 0);
    lv_obj_remove_flag(_fc_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_opa(_fc_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_arc_color(_fc_arc, lv_color_hex(0x7C3AED), LV_PART_INDICATOR);

    _fc_time_lbl = lv_label_create(_fc_arc);
    lv_obj_set_style_text_color(_fc_time_lbl, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_text_font(_fc_time_lbl, &lv_font_montserrat_16, 0);
    lv_label_set_text(_fc_time_lbl, "25:00");
    lv_obj_center(_fc_time_lbl);

    _fc_buddy_lbl = lv_label_create(_fc_run_box);
    lv_obj_set_pos(_fc_buddy_lbl, 6, 140);
    lv_obj_set_width(_fc_buddy_lbl, 200);
    lv_obj_set_style_text_align(_fc_buddy_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(_fc_buddy_lbl, lv_color_hex(0x4C1D95), 0);
    lv_obj_set_style_text_font(_fc_buddy_lbl, &lv_font_montserrat_12, 0);
    lv_label_set_text(_fc_buddy_lbl, "Studying quietly...");

    make_btn(_fc_run_box, 36, 174, 140, 38, 0xFEE2E2, 0x991B1B, "End Sprint",
             [](lv_event_t*) { if (_cbs.onStopFocus) _cbs.onStopFocus(); }, nullptr);
}

// =============================================================================
// Public API
// =============================================================================
void ui_screens_init(const UiCallbacks& cbs) {
    _cbs = cbs;
    zoo_renderer_init();
    build_adopt_screens();
    build_home_screen();
    build_care_screen();
    build_play_screen();
    build_shop_screen();
    build_journal_screen();
    build_memorial_screen();
    build_settings_screen();
    build_minigame_screen();
    build_study_hub_screen();
    build_quiz_screen();
    build_focus_screen();
}

void ui_show_screen(UiScreen scr) {
    _cur = scr;
    switch (scr) {
    case UiScreen::ADOPT_CAT:     lv_screen_load(_scr_adopt_cat); break;
    case UiScreen::ADOPT_SPECIES: lv_screen_load(_scr_adopt_sp);  break;
    case UiScreen::HOME:          lv_screen_load(_scr_home);      break;
    case UiScreen::CARE:          lv_screen_load(_scr_care);      break;
    case UiScreen::PLAY:          lv_screen_load(_scr_play);      break;
    case UiScreen::SHOP:          lv_screen_load(_scr_shop);      break;
    case UiScreen::JOURNAL:       lv_screen_load(_scr_journal);   break;
    case UiScreen::MEMORIAL:      lv_screen_load(_scr_memorial);  break;
    case UiScreen::SETTINGS:      lv_screen_load(_scr_settings);  break;
    case UiScreen::MINIGAME:      lv_screen_load(_scr_minigame);  break;
    case UiScreen::STUDY_HUB:     lv_screen_load(_scr_study_hub); break;
    case UiScreen::QUIZ:          lv_screen_load(_scr_quiz);      break;
    case UiScreen::FOCUS:         lv_screen_load(_scr_focus);     break;
    }
}

UiScreen ui_current_screen() { return _cur; }
lv_obj_t* ui_minigame_parent() { return _scr_minigame; }

void ui_show_speech_bubble(const char* text, uint32_t duration_ms) {
    if (!_bubble_box || !_bubble_lbl || !text) return;
    lv_label_set_text(_bubble_lbl, text);
    lv_obj_remove_flag(_bubble_box, LV_OBJ_FLAG_HIDDEN);
    _bubble_until = millis() + duration_ms;
}

void ui_hide_speech_bubble() {
    if (_bubble_box) lv_obj_add_flag(_bubble_box, LV_OBJ_FLAG_HIDDEN);
    _bubble_until = 0;
}

void ui_update_home(const ZooPetState* pet, const AppConfig* cfg,
                    uint8_t local_hour, uint8_t local_min,
                    uint8_t battery_pct, bool wifi_on, uint32_t now_ms) {
    if (!pet) return;
    const SpeciesProfile& sp = species_get(pet->speciesId);

    if (_bubble_until > 0 && now_ms >= _bubble_until) {
        ui_hide_speech_bubble();
    }

    if ((now_ms - _last_anim_ms) >= 350) {
        _last_anim_ms = now_ms;
        _anim_frame++;
        if (_cur == UiScreen::HOME) {
            zoo_renderer_draw(_pet_canvas, pet, now_ms, _anim_frame);
        }
    }

    if (_cur == UiScreen::HOME) {
        lv_label_set_text_fmt(_lbl_clock, "%02u:%02u", (unsigned)local_hour, (unsigned)local_min);
        lv_label_set_text(_lbl_title, pet->petName);
        lv_label_set_text_fmt(_lbl_status_r, "%s%u%%", wifi_on ? "W " : "", (unsigned)battery_pct);

        PetMood mood = zoo_compute_mood(pet, now_ms);
        const char* st_name = sp.stageNames[pet->stage < LIFE_STAGE_COUNT ? pet->stage : 0];
        if (sp.isKarimono) {
            lv_label_set_text_fmt(_lbl_stage, "Gen %u %s · %s",
                                  (unsigned)pet->genome.generation, st_name, zoo_mood_name(mood));
        } else {
            lv_label_set_text_fmt(_lbl_stage, "%s · %s · %s",
                                  sp.name, st_name, zoo_mood_name(mood));
        }

        lv_bar_set_value(_bar_v[0], pet->hunger,     LV_ANIM_OFF);
        lv_bar_set_value(_bar_v[1], pet->hydration,  LV_ANIM_OFF);
        lv_bar_set_value(_bar_v[2], pet->envCare,    LV_ANIM_OFF);
        lv_bar_set_value(_bar_v[3], pet->enrichment, LV_ANIM_OFF);
        lv_label_set_text_fmt(_lbl_coins, "%luc · %lu steps · Hlt %u%% · Bond %u%%",
                              (unsigned long)pet->zooCoins, (unsigned long)pet->stepsToday,
                              (unsigned)pet->health, (unsigned)pet->bond);
    }

    if (_cur == UiScreen::CARE) {
        lv_label_set_text_fmt(_care_lbl[0], "Feed: %s", sp.primaryFood);
        lv_label_set_text_fmt(_care_lbl[1], "Treat: %s", sp.treatFood);
        lv_label_set_text_fmt(_care_lbl[2], "%s (%u%%)", sp.envCareLabel, (unsigned)pet->envCare);
        lv_label_set_text_fmt(_care_lbl[3], pet->sick ? "Vet Medicine (Need!)" : "Vet Checkup");
        lv_label_set_text(_care_lbl[4], pet->manualRest ? "Wake Pet Up" : "Cozy Rest / Nap");
    }

    if (_cur == UiScreen::SHOP) {
        lv_label_set_text_fmt(_shop_coins, "Zoo Coins: %lu", (unsigned long)pet->zooCoins);
        for (uint8_t i = 0; i < SHOP_ITEM_COUNT; i++) {
            const ShopItem& it = shop_get_item(i);
            if (it.isAccessory && pet->inventory[i] > 0) {
                lv_label_set_text_fmt(_shop_btn_lbl[i], "%s (%s)",
                    it.name, pet->equippedAccessory == i ? "Equipped" : "Owned");
            } else {
                lv_label_set_text_fmt(_shop_btn_lbl[i], "%s - %uc", it.name, (unsigned)it.price);
            }
        }
    }

    if (_cur == UiScreen::JOURNAL) {
        if (sp.isKarimono) {
            char code[13];
            karimono_encode_code(pet->genome, code);
            lv_label_set_text_fmt(_j_body,
                "%s (%s)\nGen %u · Parents: %s x %s\nCode: %s\n"
                "Play/Calm: %d  Brave: %d\nWarm: %d  Curious: %d\n"
                "Total Steps: %lu · %s",
                pet->petName, sp.name, (unsigned)pet->genome.generation,
                pet->genome.parentAName, pet->genome.parentBName, code,
                (int)pet->playfulCalm, (int)pet->braveShy,
                (int)pet->affectionIndep, (int)pet->curiousRoutine,
                (unsigned long)pet->stepsTotal, sp.funFacts[0]);
            lv_label_set_text(_j_breed_lbl, zoo_karimono_can_breed(pet)
                ? "Cross-Breed (Plaza)" : "Breed at Adult Stage");
        } else {
            lv_label_set_text_fmt(_j_body,
                "%s (%s)\n%s · %s\nAge: %luh / %uh (Permadeath)\n"
                "Play/Calm: %d  Brave: %d\nWarm: %d  Curious: %d\n"
                "Steps: %lu · Memorials: %u\n%s",
                pet->petName, sp.scientificName,
                sp.biomeName, circadian_name(sp.circadian),
                (unsigned long)(pet->ageSeconds / 3600UL), (unsigned)sp.naturalLifespanHours,
                (int)pet->playfulCalm, (int)pet->braveShy,
                (int)pet->affectionIndep, (int)pet->curiousRoutine,
                (unsigned long)pet->stepsTotal, (unsigned)memorial_count(),
                sp.funFacts[(pet->ageSeconds / 60) % 3]);
            lv_label_set_text(_j_breed_lbl, "Adopt New Animal");
        }
    }
    (void)cfg;
}

void ui_refresh_settings(const AppConfig* cfg, bool wifi_on, const char* ssid, const char* url) {
    if (!cfg) return;
    lv_label_set_text(_s_wifi_lbl, wifi_on ? "Wi-Fi Phone Chat: ON" : "Wi-Fi Phone Chat: OFF");
    if (wifi_on && url && url[0]) {
        lv_label_set_text_fmt(_s_url_lbl, "Wi-Fi: %s  %s", ssid ? ssid : "", url);
        if (strncmp(url, "http://", 7) == 0) {
            lv_qrcode_update(_s_qr, url, strlen(url));
            lv_obj_remove_flag(_s_qr, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_label_set_text(_s_url_lbl, "Toggle Wi-Fi to chat/upload decks");
        lv_obj_add_flag(_s_qr, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(_s_mute_lbl, cfg->buzzer_enabled ? "Sound: ON" : "Sound: MUTE");
    lv_label_set_text(_s_demo_lbl, cfg->demo_speed ? "Demo: 30x" : "Demo: 1x");
    lv_label_set_text(_s_bgr_lbl,  cfg->bgr_order ? "BGR: ON" : "RGB");
}

void ui_refresh_memorial(const ZooPetState* dead_pet) {
    if (!dead_pet) return;
    const SpeciesProfile& sp = species_get(dead_pet->speciesId);
    const char* why = (dead_pet->deathCause == DeathCause::NATURAL_OLD_AGE)
        ? "Passed peacefully at full natural age."
        : "Passed away from severe illness.";
    lv_label_set_text_fmt(_m_body,
        "In Loving Memory of %s\n"
        "Species: %s\n"
        "%s\n\n"
        "Lived: %lu hours\n"
        "Final Bond: %u%%\n"
        "Total Zoo Memorials: %u",
        dead_pet->petName, sp.name, why,
        (unsigned long)(dead_pet->ageSeconds / 3600UL),
        (unsigned)dead_pet->bond,
        (unsigned)memorial_count());
}

void ui_refresh_study_hub(int64_t now_epoch) {
    const StudyStats& st = study_get_stats();
    uint16_t due_all = study_total_due_count(now_epoch);
    uint16_t mast    = study_mastered_count();
    lv_label_set_text_fmt(_st_summary, "XP %lu · %ud Streak · %u Mast · %u Due",
                          (unsigned long)st.xpTotal, (unsigned)st.studyStreakDays,
                          (unsigned)mast, (unsigned)due_all);

    for (uint8_t d = 0; d < (uint8_t)StudyDeckId::COUNT; d++) {
        StudyDeckId did = (StudyDeckId)d;
        uint8_t cnt = study_deck_card_count(did);
        uint8_t due = study_due_count(did, now_epoch);
        if (cnt == 0 && did == StudyDeckId::CUSTOM) {
            lv_label_set_text(_st_deck_lbl[d], "Custom Deck (Add via Wi-Fi)");
        } else {
            lv_label_set_text_fmt(_st_deck_lbl[d], "%s (%u/%u due)",
                                  study_deck_name(did), (unsigned)due, (unsigned)cnt);
        }
    }
}

void ui_show_quiz_card(const char* deck_name, uint8_t q_num, uint8_t q_total, const StudyCard* card) {
    if (!card) return;
    lv_label_set_text_fmt(_qz_hdr, "%s %u/%u", deck_name ? deck_name : "Quiz", (unsigned)q_num, (unsigned)q_total);
    lv_label_set_text(_qz_prompt, card->prompt);
    lv_obj_add_flag(_qz_fb_box, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(_qz_btn[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(_qz_btn_lbl[i], lv_color_hex(0x1E293B), 0);
        lv_label_set_text(_qz_btn_lbl[i], card->choices[i]);
    }
}

void ui_show_quiz_feedback(uint8_t picked_idx, uint8_t correct_idx, const char* takeaway) {
    for (uint8_t i = 0; i < 4; i++) {
        if (i == correct_idx) {
            lv_obj_set_style_bg_color(_qz_btn[i], lv_color_hex(0xDCFCE7), 0);
        } else if (i == picked_idx && picked_idx != correct_idx) {
            lv_obj_set_style_bg_color(_qz_btn[i], lv_color_hex(0xFEE2E2), 0);
        }
    }
    bool ok = (picked_idx == correct_idx);
    lv_obj_set_style_bg_color(_qz_fb_box, lv_color_hex(ok ? 0x166534 : 0x991B1B), 0);
    lv_label_set_text_fmt(_qz_fb_lbl, "%s %s\n(Tap here for Next)",
                          ok ? "Correct!" : "Review:", takeaway ? takeaway : "");
    lv_obj_remove_flag(_qz_fb_box, LV_OBJ_FLAG_HIDDEN);
}

void ui_refresh_focus(bool active, uint16_t duration_min, uint32_t rem_sec, uint8_t pct,
                      bool demo_speed, const char* pet_name) {
    if (active) {
        lv_obj_add_flag(_fc_idle_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(_fc_run_box, LV_OBJ_FLAG_HIDDEN);
        lv_arc_set_value(_fc_arc, pct);
        lv_label_set_text_fmt(_fc_time_lbl, "%02lu:%02lu",
                              (unsigned long)(rem_sec / 60UL), (unsigned long)(rem_sec % 60UL));
        lv_label_set_text_fmt(_fc_buddy_lbl, "Studying %um with %s",
                              (unsigned)duration_min, (pet_name && pet_name[0]) ? pet_name : "Pet");
    } else {
        lv_obj_remove_flag(_fc_idle_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_fc_run_box, LV_OBJ_FLAG_HIDDEN);
        if (demo_speed) lv_obj_remove_flag(_fc_demo_btn, LV_OBJ_FLAG_HIDDEN);
        else            lv_obj_add_flag(_fc_demo_btn, LV_OBJ_FLAG_HIDDEN);
    }
}
