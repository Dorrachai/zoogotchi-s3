#include "buttons.h"
#include "../config.h"
#include <Arduino.h>

static constexpr uint8_t QUEUE_CAP = 8;
static ButtonEvent _queue[QUEUE_CAP];
static uint8_t _head = 0, _tail = 0;

struct OnboardBtn {
    uint8_t  pin;
    bool     lastRaw;
    bool     debounced;
    uint32_t lastChangeMs;
    uint32_t pressedAtMs;
    bool     longFired;
    bool     pendingShort;
    uint32_t lastReleaseMs;
};

static OnboardBtn _pwr  = {PIN_SYS_OUT,  false, false, 0, 0, false, false, 0};
static OnboardBtn _boot = {PIN_BOOT_BTN, false, false, 0, 0, false, false, 0};

static void enqueue(ButtonEvent e) {
    uint8_t next = (_tail + 1) & (QUEUE_CAP - 1);
    if (next != _head) {
        _queue[_tail] = e;
        _tail = next;
    }
}

void buttons_init() {
    // CRITICAL: PIN_SYS_OUT (GPIO40) is an input sense line — NEVER drive OUTPUT!
    pinMode(PIN_SYS_OUT, INPUT);
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
}

static void poll_btn(OnboardBtn& b, bool is_pwr, uint32_t now) {
    bool raw = (digitalRead(b.pin) == LOW);
    if (raw != b.lastRaw) {
        b.lastRaw = raw;
        b.lastChangeMs = now;
    }
    if ((now - b.lastChangeMs) < BTN_DEBOUNCE_MS) return;

    bool prev = b.debounced;
    b.debounced = raw;

    if (!prev && raw) {
        b.pressedAtMs = now;
        b.longFired   = false;
    }

    if (prev && !raw && !b.longFired) {
        if (is_pwr) {
            if (b.pendingShort && (now - b.lastReleaseMs) <= BTN_DOUBLE_MS) {
                b.pendingShort = false;
                enqueue(ButtonEvent::PWR_DOUBLE);
            } else {
                b.pendingShort  = true;
                b.lastReleaseMs = now;
            }
        } else {
            enqueue(ButtonEvent::BOOT_SHORT);
        }
    }

    if (raw && !b.longFired && (now - b.pressedAtMs) >= BTN_LONG_PRESS_MS) {
        b.longFired    = true;
        b.pendingShort = false;
        enqueue(is_pwr ? ButtonEvent::PWR_LONG : ButtonEvent::BOOT_LONG);
    }

    if (is_pwr && b.pendingShort && (now - b.lastReleaseMs) > BTN_DOUBLE_MS) {
        b.pendingShort = false;
        enqueue(ButtonEvent::PWR_SHORT);
    }
}

void buttons_update() {
    uint32_t now = millis();
    poll_btn(_pwr,  true,  now);
    poll_btn(_boot, false, now);
}

ButtonEvent buttons_get_event() {
    if (_head == _tail) return ButtonEvent::NONE;
    ButtonEvent e = _queue[_head];
    _head = (_head + 1) & (QUEUE_CAP - 1);
    return e;
}
