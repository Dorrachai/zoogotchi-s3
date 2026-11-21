#include "imu_driver.h"
#include "../config.h"
#include "../hal/i2c_lock.h"
#include <Arduino.h>
#include <FastIMU.h>
#include <math.h>

static QMI8658    _imu;
static bool       _ready          = false;
static float      _accel_mag      = 1.0f;
static float      _tilt_x         = 0.0f;
static float      _tilt_y         = 0.0f;
static ShakeLevel _pending        = ShakeLevel::NONE;

static float      _shake_peak_g   = 0.0f;
static uint32_t   _shake_start_ms = 0;
static bool       _in_shake       = false;

// Cradle / lullaby rocking detection
static float      _prev_tilt_x    = 0.0f;
static uint8_t    _rock_swings    = 0;
static uint32_t   _last_rock_ms   = 0;
static int8_t     _rock_dir       = 0;

// Lightweight pedometer (peak-valley on |a| around 1g)
static float      _lp_mag         = 1.0f;
static bool       _step_high      = false;
static uint32_t   _last_step_ms   = 0;
static uint32_t   _steps_total    = 0;
static uint32_t   _steps_unclaimed = 0;

// Wake-on-lift / motion change detection
static float      _prev_mag       = 1.0f;
static bool       _motion_wake    = false;

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void imu_driver_init() {
    i2c_bus_init();
    pinMode(PIN_IMU_INT1, INPUT);

    I2CScopeGuard guard(50);
    if (!guard.locked) return;

    calData calib = {};
    int err = _imu.init(calib, IMU_I2C_ADDR);
    if (err != 0) {
        Serial.printf("[imu] QMI8658 init FAILED (err=%d)\n", err);
        _ready = false;
        return;
    }
    _ready = true;
    Serial.println("[imu] QMI8658 ready");
}

void imu_driver_update() {
    if (!_ready) return;

    AccelData a;
    {
        I2CScopeGuard guard(20);
        if (!guard.locked) return;
        _imu.update();
        _imu.getAccel(&a);
    }

    float mag = sqrtf(a.accelX * a.accelX +
                      a.accelY * a.accelY +
                      a.accelZ * a.accelZ);
    _accel_mag = mag;

    if (fabsf(mag - _prev_mag) > 0.25f) {
        _motion_wake = true;
    }
    _prev_mag = mag;

    // Low-pass tilt estimation
    _tilt_x = 0.72f * _tilt_x + 0.28f * clampf(a.accelX, -1.0f, 1.0f);
    _tilt_y = 0.72f * _tilt_y + 0.28f * clampf(a.accelY, -1.0f, 1.0f);

    uint32_t now = millis();

    // Shake detection
    if (mag >= SHAKE_HARD_G) {
        if (!_in_shake) {
            _in_shake       = true;
            _shake_start_ms = now;
            _shake_peak_g   = mag;
        } else {
            if (mag > _shake_peak_g) _shake_peak_g = mag;
            if ((now - _shake_start_ms) >= SHAKE_HARD_MS && _pending == ShakeLevel::NONE) {
                _pending = ShakeLevel::HARD;
            }
        }
    } else if (mag >= SHAKE_GENTLE_G) {
        if (!_in_shake) {
            _in_shake       = true;
            _shake_start_ms = now;
            _shake_peak_g   = mag;
        } else {
            if (mag > _shake_peak_g) _shake_peak_g = mag;
            if ((now - _shake_start_ms) >= SHAKE_GENTLE_MS && _pending == ShakeLevel::NONE) {
                _pending = ShakeLevel::GENTLE;
            }
        }
    } else {
        _in_shake     = false;
        _shake_peak_g = 0.0f;
    }

    // Gentle rhythmic cradle rocking (alternating tilt reversals without hard shake)
    float dx = _tilt_x - _prev_tilt_x;
    _prev_tilt_x = _tilt_x;
    if (mag < SHAKE_GENTLE_G) {
        int8_t dir = (dx > 0.04f) ? 1 : (dx < -0.04f ? -1 : 0);
        if (dir != 0 && dir != _rock_dir) {
            _rock_dir = dir;
            if ((now - _last_rock_ms) < 1200) {
                if (_rock_swings < 10) _rock_swings++;
            } else {
                _rock_swings = 1;
            }
            _last_rock_ms = now;
        }
    } else {
        _rock_swings = 0;
    }
    if ((now - _last_rock_ms) > 1600) {
        _rock_swings = 0;
    }

    // Step counter (walking cadence ~0.3s..1.4s between peaks)
    _lp_mag = 0.85f * _lp_mag + 0.15f * mag;
    float hp = mag - _lp_mag;
    if (!_step_high && hp > 0.16f && (now - _last_step_ms) >= 300) {
        _step_high = true;
    } else if (_step_high && hp < -0.05f) {
        _step_high = false;
        _last_step_ms = now;
        _steps_total++;
        _steps_unclaimed++;
    }
}

ShakeLevel imu_get_shake() {
    ShakeLevel s = _pending;
    _pending = ShakeLevel::NONE;
    return s;
}

float imu_get_accel_mag() { return _accel_mag; }
float imu_get_tilt_x()    { return _tilt_x; }
float imu_get_tilt_y()    { return _tilt_y; }

bool imu_is_rocking_lullaby() {
    return _rock_swings >= 4;
}

uint32_t imu_get_steps() { return _steps_total; }

uint32_t imu_consume_new_steps() {
    uint32_t s = _steps_unclaimed;
    _steps_unclaimed = 0;
    return s;
}

bool imu_consume_motion_wake() {
    bool w = _motion_wake;
    _motion_wake = false;
    return w;
}
