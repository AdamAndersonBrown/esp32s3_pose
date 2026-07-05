import os

fusion_cpp = os.path.join("main", "fusion", "eskf_fusion.cpp")

with open(fusion_cpp, "r") as f:
    content = f.read()

# 1. Clean the flawed logic in eskf_physics_task
start_marker = "// ====================================================================\n            // 1. AEROSPACE ELLIPSOID CALIBRATION (Soft & Hard Iron)"
end_marker = "current_q.q0 = ekf13->X.data[0];"

start_idx = content.find(start_marker)
end_idx = content.find(end_marker)

if start_idx != -1 and end_idx != -1:
    old_block = content[start_idx:end_idx]
    new_block = """// ====================================================================
            // 1. STANDARD HARD-IRON CALIBRATION
            // ====================================================================
            float hx = sensor_data.mag_x;
            float hy = sensor_data.mag_y;
            float hz = sensor_data.mag_z;

            if (mag_profile.is_calibrated) {
                hx -= mag_profile.offset_x;
                hy -= mag_profile.offset_y;
                hz -= mag_profile.offset_z;
            }

            // ARCHITECT FIX: Restored the missing telemetry logging block
            static uint32_t telemetry_counter = 0;
            if (telemetry_counter++ % 50 == 0) {
                ESP_LOGI(TAG, "--- ESKF INPUT (CALIBRATED XYZ) ---");
                ESP_LOGI(TAG, "ACC | X: %7.0f | Y: %7.0f | Z: %7.0f", sensor_data.acc_x, sensor_data.acc_y, sensor_data.acc_z);
                ESP_LOGI(TAG, "GYR | X: %7.0f | Y: %7.0f | Z: %7.0f", sensor_data.gyr_x, sensor_data.gyr_y, sensor_data.gyr_z);
                ESP_LOGI(TAG, "MAG | X: %7.0f | Y: %7.0f | Z: %7.0f", hx, hy, hz);
                ESP_LOGI(TAG, "-------------------------------");
            }

            // Load back into arrays for the DSP Matrix
            float acc_arr[3] = {sensor_data.acc_x, sensor_data.acc_y, sensor_data.acc_z};
            float gyr_arr[3] = {sensor_data.gyr_x, sensor_data.gyr_y, sensor_data.gyr_z};
            float mag_arr[3] = {hx, hy, hz};

            dspm::Mat gyro_input_mat(gyr_arr, 3, 1);
            dspm::Mat accel_input_mat(acc_arr, 3, 1);
            dspm::Mat mag_input_mat(mag_arr, 3, 1);

            accel_input_mat = accel_input_mat / 32768.0f * 16.0f;
            gyro_input_mat *= (2000.0f * DEG_TO_RAD / 32768.0f);

            // ====================================================================
            // 2. DYNAMIC COVARIANCE SCALING (The Aerospace Leash)
            // ====================================================================
            float current_mag_norm = mag_input_mat.norm();
            
            // Latch the pristine Earth field norm directly after boot/calibration
            if (pristine_norm == 0.0f && mag_profile.is_calibrated && current_mag_norm > 10.0f) {
                pristine_norm = current_mag_norm;
                ESP_LOGI(TAG, "Latched Pristine Magnetic Radius: %.1f uT", pristine_norm);
            }

            float spherical_distortion = 0.0f;
            if (pristine_norm > 0.0f) {
                spherical_distortion = fabsf(current_mag_norm - pristine_norm) / pristine_norm;
                
                // Micro-adaptation for regional geographic drift over long periods
                if (spherical_distortion < 0.05f) {
                    pristine_norm = (pristine_norm * 0.9999f) + (current_mag_norm * 0.0001f);
                }
            }

            // Safe Normalization for ESKF ingestion
            if (current_mag_norm > 0.001f) {
                mag_input_mat = (1.0f / current_mag_norm) * mag_input_mat;
            }

            // Step 1: Execute Free Integration of High-Frequency Kinematics
            ekf13->Process(gyro_input_mat.data, dt);

            // ====================================================================
            // 3. CONTINUOUS SOFT-GATING UPDATE
            // ====================================================================
            if (sensor_data.mag_valid) {
                // Base Variances. We elevate the base mag variance slightly from 0.03 
                // to 0.10 to permanently widen the Mahalanobis gate, preventing the 
                // "Arrogance Lockout" after long walks without hacking the P-Matrix.
                float acc_var = 0.5f;
                float mag_var = 0.10f; 

                // Dynamic Inflation: If the sphere distorts (walking past metal), 
                // we exponentially inflate the measurement variance. This tells the EKF 
                // to natively trust the gyro and ignore the bad mag data smoothly,
                // eliminating the violent "jumping" caused by binary ON/OFF logic.
                if (spherical_distortion > 0.05f) {
                    mag_var += (spherical_distortion * 15.0f); 
                }

                float R_m[6] = {acc_var, acc_var, acc_var, mag_var, mag_var, mag_var}; 
                ekf13->UpdateRefMeasurementMagn(accel_input_mat.data, mag_input_mat.data, R_m);
            }

            // Extract post-update Quaternions for LVGL Graphics Engine
            """
    content = content.replace(old_block, new_block)
    print("Fixed physics task logic.")
else:
    print("Could not find start/end markers for physics task.")

# 2. Restore the fallback hard-iron offsets in the init function
old_offsets = """        // Inject the precise SVD Hard-Iron center extracted from your CALIB_CSV telemetry
        mag_profile.offset_x = -32.48079f;
        mag_profile.offset_y = 8.22385f;
        mag_profile.offset_z = -2.14308f;"""

new_offsets = """        // Restored your original native fallbacks
        mag_profile.offset_x = -143.5f;
        mag_profile.offset_y = 85.0f;
        mag_profile.offset_z = 325.0f;"""

if old_offsets in content:
    content = content.replace(old_offsets, new_offsets)
    print("Fixed fallback offsets.")
else:
    print("Could not find fallback offsets to replace.")

with open(fusion_cpp, "w") as f:
    f.write(content)