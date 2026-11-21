#include "lvgl_port.h"
#include "../config.h"
#include <Arduino.h>

LGFX gfx;

static lv_display_t* _disp          = nullptr;
static lv_color_t*   _buf1          = nullptr;
static lv_color_t*   _buf2          = nullptr;
static uint8_t       _target_bright = LCD_BL_NORMAL;
static bool          _panel_asleep  = false;

static uint8_t  _flash_remain  = 0;
static uint32_t _flash_next_ms = 0;
static bool     _flash_bright  = false;

static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    gfx.startWrite();
    gfx.setAddrWindow(area->x1, area->y1, w, h);
    gfx.writePixels(reinterpret_cast<lgfx::rgb565_t*>(px_map), w * h);
    gfx.endWrite();

    lv_display_flush_ready(disp);
}

void lvgl_port_init(bool bgr_order) {
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);

    gfx.setBgrOrder(bgr_order);
    gfx.init();
    gfx.setRotation(0);
    gfx.setBrightness(LCD_BL_NORMAL);
    gfx.fillScreen(TFT_BLACK);

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });

    const size_t buf_size = LCD_WIDTH * LV_BUF_LINES * sizeof(lv_color_t);
    _buf1 = reinterpret_cast<lv_color_t*>(ps_malloc(buf_size));
    _buf2 = reinterpret_cast<lv_color_t*>(ps_malloc(buf_size));
    if (!_buf1) _buf1 = reinterpret_cast<lv_color_t*>(malloc(buf_size));
    if (!_buf2) _buf2 = reinterpret_cast<lv_color_t*>(malloc(buf_size));

    _disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    lv_display_set_flush_cb(_disp, flush_cb);
    lv_display_set_buffers(_disp, _buf1, _buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    Serial.printf("[display] LVGL v%d.%d.%d init OK\n",
                  LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
}

void display_alert_flash(uint8_t flashes) {
    if (_panel_asleep) return;
    _flash_remain  = flashes * 2;
    _flash_bright  = true;
    _flash_next_ms = millis();
}

void lvgl_port_tick() {
    if (!_panel_asleep) {
        lv_timer_handler();
    }
    if (_flash_remain > 0) {
        uint32_t now = millis();
        if (now >= _flash_next_ms) {
            gfx.setBrightness(_flash_bright ? 255 : 0);
            _flash_bright = !_flash_bright;
            _flash_remain--;
            _flash_next_ms = now + 80;
            if (_flash_remain == 0) {
                gfx.setBrightness(_target_bright);
            }
        }
    }
}

void lvgl_port_set_brightness(uint8_t b) {
    _target_bright = b;
    if (_flash_remain == 0 && !_panel_asleep) {
        gfx.setBrightness(b);
    }
}

void lvgl_port_set_panel_sleep(bool sleep) {
    if (_panel_asleep == sleep) return;
    _panel_asleep = sleep;
    if (sleep) {
        _flash_remain = 0;
        gfx.setBrightness(0);
        gfx.sleep();
    } else {
        gfx.wakeup();
        gfx.setBrightness(_target_bright);
    }
}

lv_display_t* lvgl_get_disp() { return _disp; }
