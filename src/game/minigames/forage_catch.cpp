#include "forage_catch.h"
#include "../../config.h"
#include "../../buzzer/buzzer.h"
#include <Arduino.h>
#include <stdio.h>

struct FallingObj {
    lv_obj_t* obj;
    float     x;
    float     y;
    float     vy;
    bool      isHazard;
    bool      active;
};

static constexpr uint8_t MAX_OBJS = 6;
static lv_obj_t*  _root       = nullptr;
static lv_obj_t*  _lbl_hud    = nullptr;
static lv_obj_t*  _basket     = nullptr;
static FallingObj _objs[MAX_OBJS];
static float      _basket_x   = 100.0f;
static uint32_t   _start_ms   = 0;
static uint32_t   _last_ms    = 0;
static uint32_t   _spawn_ms   = 0;
static uint16_t   _score      = 0;
static uint8_t    _lives      = 3;
static uint32_t   _acc_hex    = 0x22C55E;

void forage_catch_start(lv_obj_t* parent, uint32_t bg_hex, uint32_t acc_hex, const char* food_name) {
    forage_catch_stop();
    _acc_hex  = acc_hex;
    _score    = 0;
    _lives    = 3;
    _basket_x = 96.0f;
    _start_ms = millis();
    _last_ms  = _start_ms;
    _spawn_ms = _start_ms;

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
    lv_label_set_text_fmt(_lbl_hud, "Tilt: %s  0pt  HP:3", food_name ? food_name : "Forage");

    _basket = lv_obj_create(_root);
    lv_obj_set_size(_basket, 48, 16);
    lv_obj_set_pos(_basket, (int32_t)_basket_x, 224);
    lv_obj_set_style_bg_color(_basket, lv_color_hex(acc_hex), 0);
    lv_obj_set_style_radius(_basket, 8, 0);
    lv_obj_set_style_border_width(_basket, 2, 0);
    lv_obj_set_style_border_color(_basket, lv_color_hex(0x1E293B), 0);

    for (uint8_t i = 0; i < MAX_OBJS; i++) {
        _objs[i].obj = lv_obj_create(_root);
        lv_obj_set_size(_objs[i].obj, 18, 18);
        lv_obj_set_style_radius(_objs[i].obj, 9, 0);
        lv_obj_set_style_border_width(_objs[i].obj, 1, 0);
        lv_obj_set_style_border_color(_objs[i].obj, lv_color_hex(0x1E293B), 0);
        lv_obj_add_flag(_objs[i].obj, LV_OBJ_FLAG_HIDDEN);
        _objs[i].active = false;
    }
}

MiniGameResult forage_catch_update(float tilt_x, int16_t touch_x, bool touch_down, uint32_t now_ms) {
    MiniGameResult res = {};
    if (!_root) {
        res.finished = true;
        return res;
    }

    float dt = (float)(now_ms - _last_ms) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    _last_ms = now_ms;

    uint32_t elapsed = now_ms - _start_ms;
    if (elapsed >= 25000UL || _lives == 0) {
        res.finished     = true;
        res.score        = _score;
        res.coinsEarned  = (uint16_t)(4 + _score / 12);
        res.braveryDelta = 3;
        snprintf(res.summary, sizeof(res.summary), "Forage: %u pts (+%uc)",
                 (unsigned)_score, (unsigned)res.coinsEarned);
        forage_catch_stop();
        return res;
    }

    if (touch_down) {
        _basket_x = (float)touch_x - 24.0f;
    } else {
        _basket_x += tilt_x * 260.0f * dt;
    }
    if (_basket_x < 16.0f)  _basket_x = 16.0f;
    if (_basket_x > 176.0f) _basket_x = 176.0f;
    lv_obj_set_pos(_basket, (int32_t)_basket_x, 224);

    if ((now_ms - _spawn_ms) >= 620) {
        _spawn_ms = now_ms;
        for (uint8_t i = 0; i < MAX_OBJS; i++) {
            if (!_objs[i].active) {
                _objs[i].active   = true;
                _objs[i].x        = (float)(26 + (esp_random() % 170));
                _objs[i].y        = 36.0f;
                _objs[i].vy       = (float)(95 + (esp_random() % 65));
                _objs[i].isHazard = ((esp_random() % 100) < 28);
                lv_obj_set_style_bg_color(_objs[i].obj,
                    lv_color_hex(_objs[i].isHazard ? 0x64748B : 0xF59E0B), 0);
                lv_obj_set_pos(_objs[i].obj, (int32_t)_objs[i].x, (int32_t)_objs[i].y);
                lv_obj_remove_flag(_objs[i].obj, LV_OBJ_FLAG_HIDDEN);
                break;
            }
        }
    }

    for (uint8_t i = 0; i < MAX_OBJS; i++) {
        if (!_objs[i].active) continue;
        _objs[i].y += _objs[i].vy * dt;
        lv_obj_set_pos(_objs[i].obj, (int32_t)_objs[i].x, (int32_t)_objs[i].y);

        // Collision with basket
        if (_objs[i].y >= 210.0f && _objs[i].y <= 236.0f &&
            _objs[i].x >= (_basket_x - 14.0f) && _objs[i].x <= (_basket_x + 48.0f)) {
            _objs[i].active = false;
            lv_obj_add_flag(_objs[i].obj, LV_OBJ_FLAG_HIDDEN);
            if (_objs[i].isHazard) {
                if (_lives > 0) _lives--;
                buzzer_play_async(MELODY_SICK, 1);
            } else {
                _score += 10;
                buzzer_play_async(MELODY_FEED, 1);
            }
        } else if (_objs[i].y > 256.0f) {
            _objs[i].active = false;
            lv_obj_add_flag(_objs[i].obj, LV_OBJ_FLAG_HIDDEN);
        }
    }

    uint32_t rem_s = (25000UL - elapsed) / 1000UL;
    lv_label_set_text_fmt(_lbl_hud, "%lus  %upt  HP:%u",
                          (unsigned long)rem_s, (unsigned)_score, (unsigned)_lives);
    (void)_acc_hex;
    return res;
}

void forage_catch_stop() {
    if (_root) {
        lv_obj_del(_root);
        _root = nullptr;
    }
    _lbl_hud = nullptr;
    _basket  = nullptr;
    for (uint8_t i = 0; i < MAX_OBJS; i++) {
        _objs[i].obj    = nullptr;
        _objs[i].active = false;
    }
}
