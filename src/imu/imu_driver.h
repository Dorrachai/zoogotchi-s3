#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// IMU Driver — QMI8658C (I2C 0x6B) via FastIMU
// Exposes shake detection, filtered tilt (-1..+1), cradle rock, & pedometer.
// =============================================================================

enum class ShakeLevel : uint8_t { NONE = 0, GENTLE = 1, HARD = 2 };

void       imu_driver_init();
void       imu_driver_update();
ShakeLevel imu_get_shake();
float      imu_get_accel_mag();
float      imu_get_tilt_x();          // -1.0 (left) .. +1.0 (right)
float      imu_get_tilt_y();          // -1.0 (up)   .. +1.0 (down)
bool       imu_is_rocking_lullaby();  // rhythmic gentle cradle motion
uint32_t   imu_get_steps();
uint32_t   imu_consume_new_steps();
bool       imu_consume_motion_wake();
