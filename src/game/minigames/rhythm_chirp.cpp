#include "rhythm_chirp.h"
#include "../../config.h"
#include "../../buzzer/buzzer.h"
#include <Arduino.h>
#include <stdio.h>

static lv_obj_t* _root        = nullptr;
static lv_obj_t* _lbl_hud     = nullptr;
static lv_obj_t* _pads[4]     = {};
static uint8_t   _seq[6]      = {};
static uint8_t   _seq_len     = 2;
static uint8_t   _show_idx    = 0;
static uint8_t   _in_idx      = 0;
static bool      _showing     = true;
static bool      _done        = false;
static bool      _won         = false;
static uint16_t  _score       = 0;
static uint32_t  _next_step_ms = 0;

static const uint32_t PAD_BASE[4]  = {0x94A3B8, 0x94A3B8, 0x94A3B8, 0x94A3B8};
static const uint32_t PAD_LIT[4]   = {0xF59E0B, 0x10B981, 0x3B82F6, 0xEC4899};
static const MelodyNote PAD_NOTE[4][1] = {
    {{Tone::C4, 150}},
    {{Tone::E4, 150}},
    {{Tone::G4, 150}},
    {{Tone::C5, 150}},
};

static void clear_pads() {
    for (int i = 0; i < 4; i++) {
        if (_pads[i]) lv_obj_set_style_bg_color(_pads[i], lv_color_hex(PAD_BASE[i]), 0);
    }
}

static void pad_tap_cb(lv_event_t* e) {
    if (_showing || _done) return;
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx >= 4) return;

    clear_pads();
    lv_obj_set_style_bg_color(_pads[idx], lv_color_hex(PAD_LIT[idx]), 0);
    buzzer_play_async(PAD_NOTE[idx], 1);

    if (idx == _seq[_in_idx]) {
        _in_idx++;
        _score += 15;
        if (_in_idx >= _seq_len) {
            if (_seq_len >= 5) {
                _done = true;
                _won  = true;
            } else {
                _seq[_seq_len] = (uint8_t)(esp_random() % 4);
                _seq_len++;
                _showing      = true;
                _show_idx     = 0;
                _in_idx       = 0;
                _next_step_ms = millis() + 600;
            }
        }
    } else {
        _done = true;
        _won  = false;
        buzzer_play_async(MELODY_SICK, 1);
    }
}

void rhythm_chirp_start(lv_obj_t* parent, uint32_t bg_hex) {
    rhythm_chirp_stop();
    _seq_len      = 2;
    _show_idx     = 0;
    _in_idx       = 0;
    _showing      = true;
    _done         = false;
    _won          = false;
    _score        = 0;
    _next_step_ms = millis() + 450;

    for (uint8_t i = 0; i < 6; i++) _seq[i] = (uint8_t)(esp_random() % 4);

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
    lv_label_set_text(_lbl_hud, "Rhythm Chirp: Watch!");

    static const int16_t px[4] = {28, 128, 28, 128};
    static const int16_t py[4] = {54, 54, 152, 152};
    for (uint8_t i = 0; i < 4; i++) {
        _pads[i] = lv_button_create(_root);
        lv_obj_set_size(_pads[i], 84, 84);
        lv_obj_set_pos(_pads[i], px[i], py[i]);
        lv_obj_set_style_radius(_pads[i], 16, 0);
        lv_obj_set_style_bg_color(_pads[i], lv_color_hex(PAD_BASE[i]), 0);
        lv_obj_add_event_cb(_pads[i], pad_tap_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }
}

MiniGameResult rhythm_chirp_update(uint32_t now_ms) {
    MiniGameResult res = {};
    if (!_root) {
        res.finished = true;
        return res;
    }
    if (_done) {
        res.finished     = true;
        res.score        = _score;
        res.coinsEarned  = (uint16_t)(_won ? 18 : (4 + _score / 15));
        res.braveryDelta = 2;
        snprintf(res.summary, sizeof(res.summary), "%s %u pts (+%uc)",
                 _won ? "Perfect!" : "Nice try!", (unsigned)_score, (unsigned)res.coinsEarned);
        rhythm_chirp_stop();
        return res;
    }

    if (_showing && now_ms >= _next_step_ms) {
        clear_pads();
        if (_show_idx < _seq_len) {
            uint8_t pad = _seq[_show_idx++];
            lv_obj_set_style_bg_color(_pads[pad], lv_color_hex(PAD_LIT[pad]), 0);
            buzzer_play_async(PAD_NOTE[pad], 1);
            _next_step_ms = now_ms + 450;
            lv_label_set_text_fmt(_lbl_hud, "Watch (%u/%u)...", (unsigned)_show_idx, (unsigned)_seq_len);
        } else {
            _showing = false;
            lv_label_set_text_fmt(_lbl_hud, "Your turn! (%u notes)", (unsigned)_seq_len);
        }
    }
    return res;
}

void rhythm_chirp_stop() {
    if (_root) {
        lv_obj_del(_root);
        _root = nullptr;
    }
    _lbl_hud = nullptr;
    for (int i = 0; i < 4; i++) _pads[i] = nullptr;
}
