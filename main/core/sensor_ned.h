#pragma once
#include "sensor_calib.h"

// STAGE 3: GLOBAL NAVIGATION FRAME (NED)
// Goal: Translate the calibrated Body ENU frame (East, North, Up) into the 
// aerospace standard NED frame (North, East, Down) for the Extended Kalman Filter.

struct NedVectors {
    float accel[3];
    float gyro[3];
    float mag[3];
};

static inline NedVectors stage3_ned_transform(CalibratedVectors calib) {
    NedVectors ned;

    // ENU to NED Geometric Rotation
    
    // 1. ACCELEROMETER (Translating Normal Force to True Gravity)
    ned.accel[0] = -calib.accel[1];  // Gravity N = -Normal Force N
    ned.accel[1] = -calib.accel[0];  // Gravity E = -Normal Force E
    ned.accel[2] = calib.accel[2];   // Gravity D = Normal Force U

    // 2. GYROSCOPE
    ned.gyro[0] = calib.gyro[1];
    ned.gyro[1] = calib.gyro[0];
    ned.gyro[2] = -calib.gyro[2];

    // 3. MAGNETOMETER
    ned.mag[0] = calib.mag[1];
    ned.mag[1] = calib.mag[0];
    ned.mag[2] = -calib.mag[2];

    return ned;
}
