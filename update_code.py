import os
import re

app_main_path = os.path.join("main", "core", "app_main.cpp")

with open(app_main_path, "r") as f:
    content = f.read()

# Safely target the existing diagnostic block we know is present
pattern = re.compile(r"(ESP_LOGW\(TAG, \"DIAGNOSTIC -> Raw Mag Yaw:[^\n]+\);)")

replacement = r"""\1

        // ARCHITECT DIAGNOSTIC: Magnetic Spherical Validity Monitor
        // The magnitude (radius) of the magnetic vector should remain perfectly constant.
        // If the radius expands or contracts when the device is tilted, the Z-axis 
        // calibration is failing and warping the EKF virtual horizon.
        
        static float baseline_radius = 0.0f;
        static uint32_t monitor_tick = 0;
        
        // Calculate the current 3D radius (Pythagorean theorem)
        float current_radius = sqrt((calib_mag[0] * calib_mag[0]) + 
                                    (calib_mag[1] * calib_mag[1]) + 
                                    (calib_mag[2] * calib_mag[2]));
                                    
        // Latch the baseline radius when sitting flat after boot
        if (baseline_radius == 0.0f && current_radius > 10.0f && monitor_tick > 100) {
            baseline_radius = current_radius;
            ESP_LOGI(TAG, "MAGNETIC MONITOR: Baseline Radius Locked at %.1f", baseline_radius);
        }
        monitor_tick++;

        if (baseline_radius > 0.0f && monitor_tick % 25 == 0) {
            float deviation = (fabs(current_radius - baseline_radius) / baseline_radius) * 100.0f;
            
            if (deviation > 10.0f) {
                ESP_LOGE(TAG, "CALIBRATION INVALID: Sphere Warped! Radius: %.1f (Deviation: %.1f%%)", current_radius, deviation);
            } else if (monitor_tick % 100 == 0) {
                // Print a heartbeat every few seconds to show it is tracking cleanly
                ESP_LOGI(TAG, "Magnetic Radius Stable: %.1f (Deviation: %.1f%%)", current_radius, deviation);
            }
        }"""

content, count = pattern.subn(replacement, content)

if count > 0:
    with open(app_main_path, "w") as f:
        f.write(content)
    print("Successfully injected the Magnetic Spherical Validity Monitor.")
else:
    print("Error: Could not locate the target block.")