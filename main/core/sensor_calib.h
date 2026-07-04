#pragma once
#include "sensor_hal.h"

// STAGE 2: MAGNETIC CALIBRATION
// Goal: Remove Hard Iron chassis bias and apply Soft Iron scaling to yield a perfect magnetic sphere.

struct CalibratedVectors {
    float accel[3];
    float gyro[3];
    float mag[3];
};

class SensorCalibrator {
private:
    float mag_min[3] = {10000.0f, 10000.0f, 10000.0f};
    float mag_max[3] = {-10000.0f, -10000.0f, -10000.0f};
    bool is_calibrated = false;

public:
    CalibratedVectors apply_calibration(BodyVectors raw) {
        CalibratedVectors calib;
        
        // Pass through IMU data (Gravity/Kinematics don't need magnetic calibration)
        for(int i=0; i<3; i++) {
            calib.accel[i] = raw.accel[i];
            calib.gyro[i] = raw.gyro[i];
        }

        // Update dynamic Hard Iron bounds
        for (int j = 0; j < 3; j++) {
            if (raw.mag[j] < mag_min[j]) mag_min[j] = raw.mag[j];
            if (raw.mag[j] > mag_max[j]) mag_max[j] = raw.mag[j];
        }

        // Apply Hard Iron Bias
        for (int j = 0; j < 3; j++) {
            calib.mag[j] = raw.mag[j];
            if ((mag_max[j] - mag_min[j]) > 30.0f) {
                float bias = (mag_max[j] + mag_min[j]) / 2.0f;
                calib.mag[j] -= bias;
                is_calibrated = true;
            }
        }

        // Apply Empirical Soft Iron Scaling (derived from your 6-Sided Box dataset)
        if (is_calibrated) {
            calib.mag[0] *= 1.1875f;
            // calib.mag[1] *= 1.0f; // Baseline
            calib.mag[2] *= 1.2881f;
        }

        return calib;
    }
};
