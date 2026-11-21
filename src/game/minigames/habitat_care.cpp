#include "habitat_care.h"
#include "../../config.h"
#include "../../buzzer/buzzer.h"
#include <Arduino.h>
#include <stdio.h>

static constexpr uint8_t NODE_COUNT = 6;
static lv_obj_t* _root       = nullptr;
static lv_obj_t* _lbl_hud    = nullptr;
static lv_obj_t* _nodes[NODE_COUNT];
static bool      _lit[NODE_COUNT];
static uint32_t  _start_ms   = 0;
static uint32_t  _last_flip  = 0;
static uint16_t  _score      = 0;
static uint32_t  _acc_hex    = 0x0EA5E9;

static void node_tap_cb(lv_event_t* e) {
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx >= NODE_COUNT || !_lit[idx]) return;
    _lit[idx] = false;
    _score   += 12;
    lv_obj_set_style_bg_color(_nodes[idx], lv_color_hex(0xCBD5E1), 0);
    buzzer_play_async(MELODY_FEED, 1);
}

void habitat_care_start(lv_obj_t* parent, uint32_t bg_hex, uint32_t acc_hex, const char* care_label) {
    habitat_care_stop();
    _acc_hex   = acc_hex;
    _score     = 0;
    _start_ms  = millis();
    _last_flip = _start_ms;

    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(_root, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_pad_all(_root, 0, 0);
    lv_obj_remove_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    _lbl_hud = lv_label_create(_root);
    lv_obj_set_pos(_lbl_hud, 24, 14);
    lv_obj_set_width(_lbl_hud, 192);
    lv_obj_set_style_text_color(_lbl_hud, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_text_font(_lbl_hud, &lv_font_montserrat_12, 0);
    lv_label_set_text_fmt(_lbl_hud, "Tap %s! 0pt", care_label ? care_label : "Bubbles");

    static const int16_t xs[6] = {36, 134, 36, 134, 36, 134};
    static const int16_t ys[6] = {46, 46, 114, 114, 182, 182};

    for (uint8_t i = 0; i < NODE_COUNT; i++) {
        _lit[i] = false;
        _nodes[i] = lv_button_create(_root);
        lv_obj_set_size(_nodes[i], 68, 56);
        lv_obj_set_pos(_nodes[i], xs[i], ys[i]);
        lv_obj_set_style_radius(_nodes[i], 28, 0);
        lv_obj_set_style_bg_color(_nodes[i], lv_color_hex(0xCBD5E1), 0);
        lv_obj_add_event_cb(_nodes[i], node_tap_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }
}

MiniGameResult habitat_care_update(uint32_t now_ms) {
    MiniGameResult res = {};
    if (!_root) {
        res.finished = true;
        return res;
    }
    uint32_t elapsed = now_ms - _start_ms;
    if (elapsed >= 20000UL) {
        res.finished     = true;
        res.score        = _score;
        res.coinsEarned  = (uint16_t)(5 + _score / 15);
        res.braveryDelta = 2;
        snprintf(res.summary, sizeof(res.summary), "Habitat Tap: %u pts (+%uc)",
                 (unsigned)_score, (unsigned)res.coinsEarned);
        habitat_care_stop();
        return res;
    }

    if ((now_ms - _last_flip) >= 650) {
        _last_flip = now_ms;
        uint8_t pick = (uint8_t)(esp_random() % NODE_COUNT);
        _lit[pick] = true;
        lv_obj_set_style_bg_color(_nodes[pick], lv_color_hex(_acc_hex), 0);
    }

    uint32_t rem_s = (20000UL - elapsed) / 1000UL;
    lv_label_set_text_fmt(_lbl_hud, "Tap! %lus   Score: %u",
                          (unsigned long)rem_s, (unsigned)_score);
    return res;
}

void habitat_care_stop() {
    if (_root) {
        lv_obj_del(_root);
        _root = nullptr;
    }
    _lbl_hud = nullptr;
    for (uint8_t i = 0; i < NODE_COUNT; i++) {
        _nodes[i] = nullptr;
        _lit[i]   = false;
    }
}
