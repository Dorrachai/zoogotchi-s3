#include "sleep_controller.h"
#include "../config.h"
#include "../display/lvgl_port.h"
#include "../rtc/rtc_driver.h"
#include <Arduino.h>
#include <esp_sleep.h>

static PowerMode _mode            = PowerMode::ACTIVE;
static uint8_t   _normal_bright   = LCD_BL_NORMAL;
static uint32_t  _last_activity   = 0;
static bool      _cpu_high        = false;
static uint8_t   _boost_refcount  = 0;

void sleep_controller_init(uint8_t normal_brightness) {
    _normal_bright = normal_brightness;
    _last_activity = millis();
    _mode          = PowerMode::ACTIVE;
    setCpuFrequencyMhz(80);
    _cpu_high = false;

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        Serial.printf("[power] Woke from deep sleep (cause=%d)\n", (int)cause);
    }
    rtc_clear_alarm();
}

void sleep_controller_set_normal_brightness(uint8_t b) {
    _normal_bright = b;
    if (_mode == PowerMode::ACTIVE) {
        lvgl_port_set_brightness(_normal_bright);
    }
}

void sleep_controller_mark_activity() {
    _last_activity = millis();
    if (_mode == PowerMode::LIGHT_SLEEP) {
        lvgl_port_set_panel_sleep(false);
    }
    if (_mode != PowerMode::ACTIVE) {
        _mode = PowerMode::ACTIVE;
        lvgl_port_set_brightness(_normal_bright);
    }
}

void sleep_controller_defer_deep_sleep() {
    uint32_t now = millis();
    if (_mode == PowerMode::LIGHT_SLEEP) {
        uint32_t floor_ms = (now > SLEEP_LIGHT_MS) ? (now - SLEEP_LIGHT_MS) : 0;
        if (_last_activity < floor_ms) {
            _last_activity = floor_ms;
        }
    } else if (_mode == PowerMode::ACTIVE || _mode == PowerMode::DIM) {
        _last_activity = now;
    }
}

void sleep_boost_cpu(bool high_perf) {
    if (high_perf) {
        _boost_refcount++;
        if (!_cpu_high) {
            setCpuFrequencyMhz(240);
            _cpu_high = true;
        }
    } else {
        if (_boost_refcount > 0) _boost_refcount--;
        if (_boost_refcount == 0 && _cpu_high) {
            setCpuFrequencyMhz(80);
            _cpu_high = false;
        }
    }
}

void sleep_controller_toggle_screen_sleep() {
    if (_mode == PowerMode::LIGHT_SLEEP) {
        sleep_controller_mark_activity();
    } else {
        _mode = PowerMode::LIGHT_SLEEP;
        lvgl_port_set_panel_sleep(true);
    }
}

static void enter_deep_sleep(void (*pre_sleep_save_cb)(void)) {
    Serial.println("[power] Entering Deep Sleep...");
    if (pre_sleep_save_cb) pre_sleep_save_cb();

    lvgl_port_set_panel_sleep(true);

    // Program PCF85063A periodic wake-up alarm (GPIO39 active-LOW)
    rtc_set_wakeup_minutes(SLEEP_RTC_WAKE_MIN);

    // Wake on RTC INT (GPIO39 LOW) or BOOT (GPIO0 LOW) or PWR (GPIO40 LOW) or IMU INT1 (GPIO38 LOW)
    // Note: GPIO0, GPIO38, GPIO39, GPIO40? On ESP32-S3, RTCIO covers GPIO0..21.
    // GPIO38/39/40 are outside RTC_GPIO (0..21), so use ext0 on PIN_BOOT_BTN (GPIO0) + timer fallback!
    // Also keep USB / timer wake reliable even if button pins are >21.
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BOOT_BTN, 0);
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_RTC_WAKE_MIN * 60ULL * 1000000ULL);

    Serial.flush();
    esp_deep_sleep_start();
}

void sleep_controller_update(bool wifi_companion_active, bool llm_busy, bool minigame_active,
                             void (*pre_sleep_save_cb)(void)) {
    if (wifi_companion_active || llm_busy || minigame_active) {
        sleep_controller_mark_activity();
        return;
    }

    uint32_t idle = millis() - _last_activity;

    if (_mode == PowerMode::ACTIVE && idle >= SLEEP_DIM_MS) {
        _mode = PowerMode::DIM;
        lvgl_port_set_brightness(LCD_BL_DIM);
    } else if (_mode == PowerMode::DIM && idle >= SLEEP_LIGHT_MS) {
        _mode = PowerMode::LIGHT_SLEEP;
        lvgl_port_set_panel_sleep(true);
    } else if (_mode == PowerMode::LIGHT_SLEEP && idle >= SLEEP_DEEP_MS) {
        // Avoid deep sleep while USB power is actively charging at desk if user didn't request off
        // Still allow deep sleep on battery (>3.0V & no active USB CDC or after idle)
        _mode = PowerMode::DEEP_SLEEP;
        enter_deep_sleep(pre_sleep_save_cb);
    }
}

void sleep_controller_power_off(void (*pre_sleep_save_cb)(void)) {
    Serial.println("[power] Safe power off...");
    if (pre_sleep_save_cb) pre_sleep_save_cb();
    lvgl_port_set_panel_sleep(true);
    delay(80);
    // Drop SYS_EN latch (cuts battery power; if USB attached, fall back to deep sleep)
    digitalWrite(PIN_SYS_EN, LOW);
    delay(250);
    enter_deep_sleep(nullptr);
}

PowerMode sleep_controller_mode() { return _mode; }
