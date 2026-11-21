#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// Buzzer driver — LEDC ch0 on GPIO42 (keep BUZZER_DUTY == 512)
// =============================================================================

struct MelodyNote {
    uint16_t freq;
    uint16_t durationMs;
};

namespace Tone {
    constexpr uint16_t C4  = 262;
    constexpr uint16_t D4  = 294;
    constexpr uint16_t E4  = 330;
    constexpr uint16_t F4  = 349;
    constexpr uint16_t G4  = 392;
    constexpr uint16_t A4  = 440;
    constexpr uint16_t B4  = 494;
    constexpr uint16_t C5  = 523;
    constexpr uint16_t D5  = 587;
    constexpr uint16_t E5  = 659;
    constexpr uint16_t G5  = 784;
    constexpr uint16_t A5  = 880;
    constexpr uint16_t SIL = 0;
}

void buzzer_init();
void buzzer_set_muted(bool muted);
bool buzzer_is_muted();
void buzzer_set_play_cb(void (*cb)(void));
void buzzer_tone(uint16_t freq, uint32_t durationMs);
void buzzer_play_async(const MelodyNote* melody, uint8_t len);
void buzzer_update();
void buzzer_stop();
bool buzzer_is_playing();
void buzzer_play_category_emote(uint8_t category_idx);

extern const MelodyNote MELODY_FEED[];
extern const MelodyNote MELODY_HAPPY[];
extern const MelodyNote MELODY_ALERT[];
extern const MelodyNote MELODY_EVOLVE[];
extern const MelodyNote MELODY_SICK[];
extern const MelodyNote MELODY_DEAD[];
extern const MelodyNote MELODY_DIZZY[];
extern const MelodyNote MELODY_MEDICINE[];
extern const MelodyNote MELODY_BOOT[];
extern const MelodyNote MELODY_PURR[];
extern const MelodyNote MELODY_BREED[];
extern const MelodyNote MELODY_LULLABY[];

extern const uint8_t MELODY_FEED_LEN;
extern const uint8_t MELODY_HAPPY_LEN;
extern const uint8_t MELODY_ALERT_LEN;
extern const uint8_t MELODY_EVOLVE_LEN;
extern const uint8_t MELODY_SICK_LEN;
extern const uint8_t MELODY_DEAD_LEN;
extern const uint8_t MELODY_DIZZY_LEN;
extern const uint8_t MELODY_MEDICINE_LEN;
extern const uint8_t MELODY_BOOT_LEN;
extern const uint8_t MELODY_PURR_LEN;
extern const uint8_t MELODY_BREED_LEN;
extern const uint8_t MELODY_LULLABY_LEN;
