#pragma once
#include "../config_mgr/config_manager.h"
#include "../zoo/zoo_engine.h"
#include "../ai/personality_memory.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// On-Demand Local Wi-Fi Companion & Phone Chat Server (ESP-IDF esp_http_server)
// Supports Home Wi-Fi STA + Automatic Fallback to Direct SoftAP "ZooGotchi-S3"
// Zero PC required — connect directly from any phone browser!
// =============================================================================

void web_companion_init(AppConfig* cfg, ZooPetState* pet, PetMemory* mem,
                        void (*on_remote_ui_cb)(const char* speech_msg));
void web_companion_start();
void web_companion_stop();
void web_companion_toggle();
void web_companion_update();

bool web_companion_is_active();
bool web_companion_is_sta_connected();
void web_companion_get_url(char* out, size_t len);
void web_companion_get_ssid(char* out, size_t len);

// Shared pet mutex for Core 0 <-> Core 1 consistency
void pet_state_lock();
void pet_state_unlock();
bool web_companion_consume_dirty();
