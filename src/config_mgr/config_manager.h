#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// AppConfig — persisted in /config.json on LittleFS
// =============================================================================

struct AppConfig {
    char     wifi_ssid[64];
    char     wifi_password[64];
    bool     demo_speed;
    bool     buzzer_enabled;
    bool     flash_alerts;
    bool     bgr_order;
    uint8_t  brightness;
    int16_t  tz_offset_min;
};

bool config_manager_load(AppConfig* cfg);
bool config_manager_save(const AppConfig* cfg);
void config_manager_print(const AppConfig* cfg);
