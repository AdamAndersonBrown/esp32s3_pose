#pragma once
#include "sensor_hal.h"

// STAGE 2: 9-DoF SENSOR CALIBRATION
// Goal: Scrub mechanical and magnetic bias from the pristine Body ENU vectors.

struct CalibratedVectors {
    float accel[3];
    float gyro[3];
    float mag[3];
};

class SensorCalibrator {
private:
    // Gyroscope State
    bool gyro_calibrated = false;
    int gyro_sample_count = 0;
    float gyro_bias[3] = {0.0f, 0.0f, 0.0f};
    const int GYRO_CALIBRATION_SAMPLES = 100;

    // Magnetometer State
    float mag_min[3] = {10000.0f, 10000.0f, 10000.0f};
    float mag_max[3] = {-10000.0f, -10000.0f, -10000.0f};
    bool mag_calibrated = false;

public:
    CalibratedVectors apply_calibration(BodyVectors raw) {
        CalibratedVectors calib;
        
        // ----------------------------------------------------
        // 1. ACCELEROMETER: Pass-through (EKF absorbs minor bias)
        // ----------------------------------------------------
        for(int i = 0; i < 3; i++) {
            calib.accel[i] = raw.accel[i];
        }

        // ----------------------------------------------------
        // 2. GYROSCOPE: Zero-Rate Offset (ZRO) Elimination
        // ----------------------------------------------------
        if (!gyro_calibrated) {
            // Accumulate samples while device is perfectly still at boot
            gyro_bias[0] += raw.gyro[0];
            gyro_bias[1] += raw.gyro[1];
            gyro_bias[2] += raw.gyro[2];
            gyro_sample_count++;

            if (gyro_sample_count >= GYRO_CALIBRATION_SAMPLES) {
                gyro_bias[0] /= GYRO_CALIBRATION_SAMPLES;
                gyro_bias[1] /= GYRO_CALIBRATION_SAMPLES;
                gyro_bias[2] /= GYRO_CALIBRATION_SAMPLES;
                gyro_calibrated = true;
            }
            
            // Output raw while calibrating
            calib.gyro[0] = raw.gyro[0];
            calib.gyro[1] = raw.gyro[1];
            calib.gyro[2] = raw.gyro[2];
        } else {
            // Subtract the stationary bias from all future readings
            calib.gyro[0] = raw.gyro[0] - gyro_bias[0];
            calib.gyro[1] = raw.gyro[1] - gyro_bias[1];
            calib.gyro[2] = raw.gyro[2] - gyro_bias[2];
        }

        // ----------------------------------------------------
        // 3. MAGNETOMETER: Hard Iron and Soft Iron Compensation
        // ----------------------------------------------------
        for (int j = 0; j < 3; j++) {
            if (raw.mag[j] < mag_min[j]) mag_min[j] = raw.mag[j];
            if (raw.mag[j] > mag_max[j]) mag_max[j] = raw.mag[j];
            
            calib.mag[j] = raw.mag[j]; // Default to raw
        }

        // Apply Hard Iron Bias (Center the sphere)
        for (int j = 0; j < 3; j++) {
            if ((mag_max[j] - mag_min[j]) > 30.0f) {
                float bias = (mag_max[j] + mag_min[j]) / 2.0f;
                calib.mag[j] -= bias;
                mag_calibrated = true; // Bounds have stretched enough to trust
            }
        }

        // Apply Soft Iron Scaling (Shape the sphere)
        if (mag_calibrated) {
            calib.mag[0] *= 1.1875f;
            // calib.mag[1] *= 1.0f; // Baseline axis
            calib.mag[2] *= 1.2881f;
        }

        return calib;
    }
};
