#include "config_manager.h"
#include "../config.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static void apply_defaults(AppConfig* cfg) {
    memset(cfg, 0, sizeof(AppConfig));
    cfg->demo_speed     = false;
    cfg->buzzer_enabled = true;
    cfg->flash_alerts   = true;
    cfg->bgr_order      = false;
    cfg->brightness     = LCD_BL_NORMAL;
    cfg->tz_offset_min  = 0;
}

bool config_manager_load(AppConfig* cfg) {
    apply_defaults(cfg);

    if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
        Serial.println("[config] LittleFS mount failed — using defaults");
        return false;
    }

    if (!LittleFS.exists("/config.json")) {
        Serial.println("[config] /config.json not found — using defaults");
        return false;
    }

    File f = LittleFS.open("/config.json", "r");
    if (!f) {
        Serial.println("[config] Failed to open /config.json — using defaults");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[config] JSON parse error: %s — using defaults\n", err.c_str());
        return false;
    }

    if (doc["wifi"]["ssid"].is<const char*>())
        strlcpy(cfg->wifi_ssid, doc["wifi"]["ssid"], sizeof(cfg->wifi_ssid));
    if (doc["wifi"]["password"].is<const char*>())
        strlcpy(cfg->wifi_password, doc["wifi"]["password"], sizeof(cfg->wifi_password));

    if (doc["game"]["demo_speed"].is<bool>())
        cfg->demo_speed = doc["game"]["demo_speed"];
    if (doc["game"]["buzzer"].is<bool>())
        cfg->buzzer_enabled = doc["game"]["buzzer"];
    if (doc["game"]["flash_alerts"].is<bool>())
        cfg->flash_alerts = doc["game"]["flash_alerts"];
    if (doc["game"]["tz_offset_min"].is<int>())
        cfg->tz_offset_min = (int16_t)doc["game"]["tz_offset_min"];

    if (doc["display"]["bgr_order"].is<bool>())
        cfg->bgr_order = doc["display"]["bgr_order"];
    if (doc["display"]["brightness"].is<int>()) {
        int b = doc["display"]["brightness"];
        if (b < 20) b = 20;
        if (b > 255) b = 255;
        cfg->brightness = (uint8_t)b;
    }

    Serial.println("[config] Loaded /config.json");
    return true;
}

bool config_manager_save(const AppConfig* cfg) {
    if (!cfg) return false;
    JsonDocument doc;
    doc["wifi"]["ssid"]           = cfg->wifi_ssid;
    doc["wifi"]["password"]       = cfg->wifi_password;
    doc["game"]["demo_speed"]     = cfg->demo_speed;
    doc["game"]["buzzer"]         = cfg->buzzer_enabled;
    doc["game"]["flash_alerts"]   = cfg->flash_alerts;
    doc["game"]["tz_offset_min"]  = cfg->tz_offset_min;
    doc["display"]["bgr_order"]   = cfg->bgr_order;
    doc["display"]["brightness"]  = cfg->brightness;

    File f = LittleFS.open("/config.tmp", "w");
    if (!f) return false;
    size_t written = serializeJson(doc, f);
    f.close();
    if (written == 0) return false;

    if (LittleFS.exists("/config.json")) LittleFS.remove("/config.json");
    return LittleFS.rename("/config.tmp", "/config.json");
}

void config_manager_print(const AppConfig* cfg) {
    Serial.printf("[config] wifi_ssid:     %s\n", cfg->wifi_ssid[0] ? cfg->wifi_ssid : "(SoftAP only)");
    Serial.printf("[config] demo_speed:    %s\n", cfg->demo_speed ? "true" : "false");
    Serial.printf("[config] buzzer:        %s\n", cfg->buzzer_enabled ? "on" : "muted");
    Serial.printf("[config] flash_alerts:  %s\n", cfg->flash_alerts ? "on" : "off");
    Serial.printf("[config] bgr_order:     %s\n", cfg->bgr_order ? "true" : "false");
    Serial.printf("[config] brightness:    %u\n", cfg->brightness);
    Serial.printf("[config] tz_offset_min: %d\n", cfg->tz_offset_min);
}
