#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ESKF and Kinematics Transformation Constants
static const float GRAVITY_EARTH = 9.80665f;
static const float IMU_ACCEL_SCALE_16G = 16.0f / 32768.0f;
static const float IMU_GYRO_SCALE_2000DPS = 2000.0f / 32768.0f;

#ifdef __cplusplus
}
#endif
