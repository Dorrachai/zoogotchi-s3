#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// Shared I2C Bus Mutex (SDA=GPIO11, SCL=GPIO10)
// Protects CST816T touch, QMI8658C IMU, and PCF85063A RTC across tasks.
// =============================================================================

void i2c_bus_init();
bool i2c_lock(uint32_t timeout_ms = 50);
void i2c_unlock();

struct I2CScopeGuard {
    bool locked;
    explicit I2CScopeGuard(uint32_t timeout_ms = 50) : locked(i2c_lock(timeout_ms)) {}
    ~I2CScopeGuard() { if (locked) i2c_unlock(); }
};
