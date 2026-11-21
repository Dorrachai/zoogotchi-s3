#include "buzzer.h"
#include "../config.h"
#include <Arduino.h>

static const MelodyNote* _mel      = nullptr;
static uint8_t           _mel_len  = 0;
static uint8_t           _mel_idx  = 0;
static uint32_t          _mel_next = 0;
static bool              _playing  = false;
static bool              _muted    = false;
static void            (*_play_cb)(void) = nullptr;

const MelodyNote MELODY_FEED[]     = {{Tone::G4, 70}, {Tone::C5, 110}};
const uint8_t    MELODY_FEED_LEN   = 2;

const MelodyNote MELODY_HAPPY[]    = {{Tone::C5, 70}, {Tone::E5, 70}, {Tone::G5, 140}};
const uint8_t    MELODY_HAPPY_LEN  = 3;

const MelodyNote MELODY_ALERT[]    = {{Tone::A4, 140}, {Tone::SIL, 90}, {Tone::A4, 140}};
const uint8_t    MELODY_ALERT_LEN  = 3;

const MelodyNote MELODY_EVOLVE[]   = {
    {Tone::C4, 70}, {Tone::E4, 70}, {Tone::G4, 70}, {Tone::C5, 80},
    {Tone::SIL, 50}, {Tone::E5, 70}, {Tone::G5, 180}
};
const uint8_t    MELODY_EVOLVE_LEN = 7;

const MelodyNote MELODY_SICK[]     = {{Tone::G4, 140}, {Tone::E4, 140}, {Tone::C4, 220}};
const uint8_t    MELODY_SICK_LEN   = 3;

const MelodyNote MELODY_DEAD[]     = {
    {Tone::E5, 220}, {Tone::SIL, 80}, {Tone::C5, 220}, {Tone::SIL, 80},
    {Tone::G4, 240}, {Tone::SIL, 80}, {Tone::C4, 420}
};
const uint8_t    MELODY_DEAD_LEN   = 7;

const MelodyNote MELODY_DIZZY[]    = {
    {Tone::G4, 55}, {Tone::E4, 55}, {Tone::G4, 55}, {Tone::E4, 55}, {Tone::D4, 80}
};
const uint8_t    MELODY_DIZZY_LEN  = 5;

const MelodyNote MELODY_MEDICINE[] = {{Tone::D5, 90}, {Tone::SIL, 40}, {Tone::G5, 140}};
const uint8_t    MELODY_MEDICINE_LEN = 3;

const MelodyNote MELODY_BOOT[]     = {
    {Tone::C4, 80}, {Tone::E4, 80}, {Tone::G4, 80}, {Tone::C5, 180}
};
const uint8_t    MELODY_BOOT_LEN   = 4;

const MelodyNote MELODY_PURR[]     = {
    {Tone::C4, 45}, {Tone::E4, 45}, {Tone::G4, 65}, {Tone::A4, 90}
};
const uint8_t    MELODY_PURR_LEN   = 4;

const MelodyNote MELODY_BREED[]    = {
    {Tone::E5, 70}, {Tone::G5, 70}, {Tone::A5, 90}, {Tone::SIL, 40},
    {Tone::G5, 70}, {Tone::A5, 200}
};
const uint8_t    MELODY_BREED_LEN  = 6;

const MelodyNote MELODY_LULLABY[]  = {
    {Tone::E4, 180}, {Tone::G4, 180}, {Tone::C5, 280}
};
const uint8_t    MELODY_LULLABY_LEN = 3;

static const MelodyNote MELODY_CHIRP_SKY[]    = {{Tone::A5, 50}, {Tone::G5, 45}, {Tone::A5, 70}};
static const MelodyNote MELODY_CHIRP_INSECT[] = {{Tone::E5, 35}, {Tone::SIL, 25}, {Tone::E5, 35}, {Tone::G5, 40}};
static const MelodyNote MELODY_CHIRP_WATER[]  = {{Tone::C4, 60}, {Tone::E4, 60}, {Tone::C5, 90}};
static const MelodyNote MELODY_CHIRP_REPTILE[]= {{Tone::D4, 90}, {Tone::F4, 110}};

void buzzer_set_muted(bool muted) {
    _muted = muted;
    if (_muted) buzzer_stop();
}
bool buzzer_is_muted() { return _muted; }
void buzzer_set_play_cb(void (*cb)(void)) { _play_cb = cb; }

void buzzer_init() {
    ledcSetup(BUZZER_LEDC_CH, 1000, BUZZER_LEDC_RES);
    ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CH);
    ledcWrite(BUZZER_LEDC_CH, 0);
}

void buzzer_tone(uint16_t freq, uint32_t durationMs) {
    if (_muted || freq == 0) {
        ledcWrite(BUZZER_LEDC_CH, 0);
    } else {
        ledcWriteTone(BUZZER_LEDC_CH, freq);
        ledcWrite(BUZZER_LEDC_CH, BUZZER_DUTY);
    }
    delay(durationMs);
    ledcWrite(BUZZER_LEDC_CH, 0);
}

void buzzer_play_async(const MelodyNote* melody, uint8_t len) {
    if (_play_cb) _play_cb();
    if (_muted || !melody || len == 0) return;
    _mel      = melody;
    _mel_len  = len;
    _mel_idx  = 0;
    _playing  = true;
    _mel_next = millis();
}

void buzzer_play_category_emote(uint8_t category_idx) {
    switch (category_idx) {
    case 1: buzzer_play_async(MELODY_CHIRP_SKY, 3); break;
    case 2: buzzer_play_async(MELODY_CHIRP_INSECT, 4); break;
    case 3: buzzer_play_async(MELODY_CHIRP_WATER, 3); break;
    case 4: buzzer_play_async(MELODY_CHIRP_REPTILE, 2); break;
    case 5: buzzer_play_async(MELODY_HAPPY, MELODY_HAPPY_LEN); break;
    default: buzzer_play_async(MELODY_PURR, MELODY_PURR_LEN); break;
    }
}

void buzzer_update() {
    if (!_playing || !_mel) return;
    uint32_t now = millis();
    if (now < _mel_next) return;

    if (_mel_idx >= _mel_len) {
        _playing = false;
        ledcWrite(BUZZER_LEDC_CH, 0);
        return;
    }

    const MelodyNote& n = _mel[_mel_idx++];
    if (n.freq == 0 || _muted) {
        ledcWrite(BUZZER_LEDC_CH, 0);
    } else {
        ledcWriteTone(BUZZER_LEDC_CH, n.freq);
        ledcWrite(BUZZER_LEDC_CH, BUZZER_DUTY);
    }
    _mel_next = now + n.durationMs;
}

void buzzer_stop() {
    _playing = false;
    ledcWrite(BUZZER_LEDC_CH, 0);
}

bool buzzer_is_playing() { return _playing; }
