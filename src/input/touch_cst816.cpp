#include "touch_cst816.h"
#include "../config.h"
#include "../hal/i2c_lock.h"
#include <Arduino.h>
#include <Wire.h>
#include <stdlib.h>

// CST816 registers
static constexpr uint8_t REG_GESTURE   = 0x01;
static constexpr uint8_t REG_CHIP_ID   = 0xA7;
static constexpr uint8_t REG_DISABLE_AUTO_SLEEP = 0xFE;

static constexpr uint8_t QUEUE_CAP = 8;
static TouchEvent _queue[QUEUE_CAP];
static uint8_t _q_head = 0, _q_tail = 0;

static bool     _ready          = false;
static bool     _pressed        = false;
static int16_t  _cur_x          = 120;
static int16_t  _cur_y          = 140;
static uint32_t _last_active_ms = 0;

// Gesture classification state
static bool     _tracking       = false;
static int16_t  _start_x        = 0;
static int16_t  _start_y        = 0;
static int16_t  _prev_x         = 0;
static int16_t  _prev_y         = 0;
static uint32_t _press_ms       = 0;
static uint32_t _last_tap_ms    = 0;
static bool     _long_fired     = false;
static uint16_t _pet_travel_px  = 0;
static uint32_t _last_pet_ms    = 0;

static uint32_t _last_poll_ms   = 0;
static lv_indev_t* _lv_indev    = nullptr;

static void enqueue_event(TouchEventType t, int16_t x, int16_t y) {
    uint8_t next = (_q_tail + 1) & (QUEUE_CAP - 1);
    if (next != _q_head) {
        _queue[_q_tail] = {t, x, y};
        _q_tail = next;
    }
}

static bool cst_read_raw(uint8_t* points, int16_t* x, int16_t* y) {
    I2CScopeGuard guard(20);
    if (!guard.locked) return false;

    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(REG_GESTURE);
    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)6) < 6) return false;
    uint8_t buf[6];
    for (int i = 0; i < 6; i++) buf[i] = Wire.read();

    *points = buf[1] & 0x0F;
    *x = ((int16_t)(buf[2] & 0x0F) << 8) | buf[3];
    *y = ((int16_t)(buf[4] & 0x0F) << 8) | buf[5];
    if (*x < 0) *x = 0;
    if (*x >= LCD_WIDTH) *x = LCD_WIDTH - 1;
    if (*y < 0) *y = 0;
    if (*y >= LCD_HEIGHT) *y = LCD_HEIGHT - 1;
    return true;
}

static void lvgl_touch_read_cb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    data->point.x = _cur_x;
    data->point.y = _cur_y;
    data->state   = _pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void touch_cst816_init() {
    i2c_bus_init();

    pinMode(PIN_TOUCH_INT, INPUT_PULLUP);
    pinMode(PIN_TOUCH_RST, OUTPUT);
    digitalWrite(PIN_TOUCH_RST, LOW);
    delay(10);
    digitalWrite(PIN_TOUCH_RST, HIGH);
    delay(50);

    I2CScopeGuard guard(50);
    if (!guard.locked) return;

    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(REG_CHIP_ID);
    if (Wire.endTransmission(false) == 0 &&
        Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)1) == 1) {
        uint8_t chip_id = Wire.read();
        // Keep touch active while device is awake
        Wire.beginTransmission(TOUCH_I2C_ADDR);
        Wire.write(REG_DISABLE_AUTO_SLEEP);
        Wire.write(0xFF);
        Wire.endTransmission();

        _ready = true;
        _last_active_ms = millis();
        Serial.printf("[touch] CST816 ready (chip=0x%02X)\n", chip_id);
    } else {
        _ready = false;
        Serial.println("[touch] CST816 not detected");
    }
}

void touch_cst816_register_lvgl() {
    if (_lv_indev) return;
    _lv_indev = lv_indev_create();
    lv_indev_set_type(_lv_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(_lv_indev, lvgl_touch_read_cb);
}

void touch_cst816_update() {
    if (!_ready) return;

    uint32_t now = millis();
    uint32_t interval = _tracking ? 5UL : 20UL;
    if ((now - _last_poll_ms) < interval) return;
    _last_poll_ms = now;

    uint8_t pts = 0;
    int16_t rx = _cur_x, ry = _cur_y;
    if (!cst_read_raw(&pts, &rx, &ry)) return;

    now = millis();
    bool down = (pts > 0);

    if (down) {
        _cur_x = rx;
        _cur_y = ry;
        _last_active_ms = now;

        if (!_tracking) {
            _tracking      = true;
            _pressed       = true;
            _start_x       = rx;
            _start_y       = ry;
            _prev_x        = rx;
            _prev_y        = ry;
            _press_ms      = now;
            _long_fired    = false;
            _pet_travel_px = 0;
        } else {
            int16_t step_dx = abs(rx - _prev_x);
            int16_t step_dy = abs(ry - _prev_y);
            _prev_x = rx;
            _prev_y = ry;

            // Petting detection inside the animal sprite viewport (y: 64..204)
            if (ry >= 64 && ry <= 204 && (step_dx + step_dy) >= 3) {
                _pet_travel_px += (uint16_t)(step_dx + step_dy);
                if (_pet_travel_px >= 42 && (now - _last_pet_ms) >= 450) {
                    _pet_travel_px = 0;
                    _last_pet_ms   = now;
                    enqueue_event(TouchEventType::PET_STROKE, rx, ry);
                }
            }

            // Long press if stationary
            if (!_long_fired && (now - _press_ms) >= 750) {
                int16_t net_dx = abs(rx - _start_x);
                int16_t net_dy = abs(ry - _start_y);
                if (net_dx < 18 && net_dy < 18) {
                    _long_fired = true;
                    enqueue_event(TouchEventType::LONG_PRESS, rx, ry);
                }
            }
        }
    } else if (_tracking) {
        _tracking = false;
        _pressed  = false;

        int16_t dx = _cur_x - _start_x;
        int16_t dy = _cur_y - _start_y;
        int16_t adx = abs(dx);
        int16_t ady = abs(dy);

        if (!_long_fired) {
            if (adx >= 44 && adx > ady * 1.4f) {
                enqueue_event(dx > 0 ? TouchEventType::SWIPE_RIGHT : TouchEventType::SWIPE_LEFT,
                              _cur_x, _cur_y);
            } else if (ady >= 44 && ady > adx * 1.4f) {
                enqueue_event(dy > 0 ? TouchEventType::SWIPE_DOWN : TouchEventType::SWIPE_UP,
                              _cur_x, _cur_y);
            } else if (adx < 20 && ady < 20 && (now - _press_ms) < 550) {
                if ((now - _last_tap_ms) <= 340) {
                    _last_tap_ms = 0;
                    enqueue_event(TouchEventType::DOUBLE_TAP, _cur_x, _cur_y);
                } else {
                    _last_tap_ms = now;
                    enqueue_event(TouchEventType::TAP, _cur_x, _cur_y);
                }
            }
        }
    }
}

bool touch_cst816_is_pressed() { return _pressed; }

void touch_cst816_get_xy(int16_t* x, int16_t* y) {
    if (x) *x = _cur_x;
    if (y) *y = _cur_y;
}

TouchEvent touch_cst816_get_event() {
    if (_q_head == _q_tail) return {TouchEventType::NONE, 0, 0};
    TouchEvent e = _queue[_q_head];
    _q_head = (_q_head + 1) & (QUEUE_CAP - 1);
    return e;
}

uint32_t touch_cst816_last_activity_ms() { return _last_active_ms; }
void touch_cst816_mark_activity() { _last_active_ms = millis(); }
