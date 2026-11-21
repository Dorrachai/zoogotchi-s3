#include "i2c_lock.h"
#include "../config.h"
#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t _i2c_mutex = nullptr;
static bool _bus_started = false;

void i2c_bus_init() {
    if (!_i2c_mutex) {
        _i2c_mutex = xSemaphoreCreateRecursiveMutex();
    }
    if (!_bus_started) {
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ);
        Wire.setTimeOut(20);
        _bus_started = true;
    }
}

bool i2c_lock(uint32_t timeout_ms) {
    if (!_i2c_mutex) i2c_bus_init();
    return xSemaphoreTakeRecursive(_i2c_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void i2c_unlock() {
    if (_i2c_mutex) {
        xSemaphoreGiveRecursive(_i2c_mutex);
    }
}
