import os
import re

calib_header_path = os.path.join("main", "core", "sensor_calib.h")
app_main_path = os.path.join("main", "core", "app_main.cpp")

# 1. GENERATE THE ISOLATED STAGE 2 MODULE
calib_content = """#pragma once
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
"""
with open(calib_header_path, "w") as f:
    f.write(calib_content)
print(f"Created isolated module: {calib_header_path}")

# 2. SURGICALLY WIRE STAGE 2 INTO APP_MAIN.CPP
with open(app_main_path, "r") as f:
    content = f.read()

# Inject the include directive if missing
if '#include "sensor_calib.h"' not in content:
    content = content.replace('#include "sensor_hal.h"', '#include "sensor_hal.h"\n#include "sensor_calib.h"')

# Inject the static calibrator instance right before the while(1) loop
if 'static SensorCalibrator calibrator;' not in content:
    content = content.replace('while (1) {', 'static SensorCalibrator calibrator;\n\n    while (1) {')

# Swap the Stage 1 telemetry block for the 9D Stage 2 comparative block
pattern = re.compile(r"// ARCHITECT FIX: Stage 1 Pipeline Isolation \(9D Verification\).*?dspm::Mat mag_input_mat\(body\.mag, 3, 1\);", re.DOTALL)

stage2_9d_integration = """// ARCHITECT FIX: Stage 2 Pipeline Isolation (9D Calibration)
        BodyVectors body = stage1_hal_transform((int16_t*)sensors_data);
        CalibratedVectors calib = calibrator.apply_calibration(body);

        static uint32_t stage2_telemetry = 0;
        if (stage2_telemetry++ % 50 == 0) {
            ESP_LOGI(TAG, "--- STAGE 2: 9D CALIBRATION VERIFICATION ---");
            ESP_LOGI(TAG, "RAW GYRO   | X: %6.0f | Y: %6.0f | Z: %6.0f", body.gyro[0], body.gyro[1], body.gyro[2]);
            ESP_LOGI(TAG, "CALIB GYRO | X: %6.0f | Y: %6.0f | Z: %6.0f", calib.gyro[0], calib.gyro[1], calib.gyro[2]);
            ESP_LOGI(TAG, "RAW MAG    | X: %6.0f | Y: %6.0f | Z: %6.0f", body.mag[0], body.mag[1], body.mag[2]);
            ESP_LOGI(TAG, "CALIB MAG  | X: %6.0f | Y: %6.0f | Z: %6.0f", calib.mag[0], calib.mag[1], calib.mag[2]);
            ESP_LOGI(TAG, "--------------------------------------------");
        }

        // Dummy feed to keep EKF from crashing. (NED mapping bypassed until Stage 3)
        dspm::Mat gyro_input_mat(calib.gyro, 3, 1);
        dspm::Mat accel_input_mat(calib.accel, 3, 1);
        dspm::Mat mag_input_mat(calib.mag, 3, 1);"""

content, count = pattern.subn(stage2_9d_integration, content)

if count > 0:
    with open(app_main_path, "w") as f:
        f.write(content)
    print(f"Successfully patched {app_main_path} for 9D Stage 2 testing.")
else:
    print("Error: Target code block not found using regex.")