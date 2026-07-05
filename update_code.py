import os

fusion_h = os.path.join("main", "fusion", "eskf_fusion.h")
fusion_cpp = os.path.join("main", "fusion", "eskf_fusion.cpp")

# 1. Update the Header to expose the API
with open(fusion_h, "r") as f:
    h_content = f.read()

if "void eskf_trigger_calibration(void);" not in h_content:
    h_content = h_content.replace(
        "void eskf_fusion_queue_data(imu_9dof_data_t *data);",
        "void eskf_fusion_queue_data(imu_9dof_data_t *data);\n\n/**\n * @brief Triggers the dynamic 3D Figure-8 Hard-Iron calibration sequence.\n */\nvoid eskf_trigger_calibration(void);"
    )
    with open(fusion_h, "w") as f:
        f.write(h_content)

# 2. Inject the State Machine into the Physics Thread
with open(fusion_cpp, "r") as f:
    cpp_content = f.read()

# Add the state variables
if "static bool is_calibrating = false;" not in cpp_content:
    cpp_content = cpp_content.replace(
        "static hard_iron_profile_t mag_profile;",
        """static hard_iron_profile_t mag_profile;

// Calibration State Machine
static bool is_calibrating = false;
static uint16_t calib_samples = 0;
static float mag_min[3] = {99999.0f, 99999.0f, 99999.0f};
static float mag_max[3] = {-99999.0f, -99999.0f, -99999.0f};"""
    )

# Expose the trigger function
if "void eskf_trigger_calibration(void)" not in cpp_content:
    cpp_content += """

void eskf_trigger_calibration(void) {
    if (!is_calibrating) {
        ESP_LOGW(TAG, "=== CALIBRATION MODE TRIGGERED ===");
        ESP_LOGW(TAG, "Please rotate device in a 3D Figure-8 for 15 seconds...");
        mag_min[0] = 99999.0f; mag_min[1] = 99999.0f; mag_min[2] = 99999.0f;
        mag_max[0] = -99999.0f; mag_max[1] = -99999.0f; mag_max[2] = -99999.0f;
        calib_samples = 0;
        is_calibrating = true;
    }
}
"""

# Inject the interception logic inside the while loop
calib_logic = """            // ARCHITECT FIX: Dynamic Field Calibration Interceptor
            if (is_calibrating && sensor_data.mag_valid) {
                // Track min/max boundaries
                if (sensor_data.mag_x < mag_min[0]) mag_min[0] = sensor_data.mag_x;
                if (sensor_data.mag_x > mag_max[0]) mag_max[0] = sensor_data.mag_x;
                
                if (sensor_data.mag_y < mag_min[1]) mag_min[1] = sensor_data.mag_y;
                if (sensor_data.mag_y > mag_max[1]) mag_max[1] = sensor_data.mag_y;
                
                if (sensor_data.mag_z < mag_min[2]) mag_min[2] = sensor_data.mag_z;
                if (sensor_data.mag_z > mag_max[2]) mag_max[2] = sensor_data.mag_z;
                
                calib_samples++;
                
                // Print a progress heartbeat every ~1 second (assuming ~60Hz loop)
                if (calib_samples % 60 == 0) {
                    ESP_LOGI(TAG, "Calibrating... [%d/900 samples collected]", calib_samples);
                }

                // 900 samples @ ~60Hz = ~15 seconds of Figure-8 rotation
                if (calib_samples >= 900) {
                    mag_profile.offset_x = (mag_max[0] + mag_min[0]) / 2.0f;
                    mag_profile.offset_y = (mag_max[1] + mag_min[1]) / 2.0f;
                    mag_profile.offset_z = (mag_max[2] + mag_min[2]) / 2.0f;
                    mag_profile.is_calibrated = true;
                    
                    eskf_save_calibration(&mag_profile);
                    is_calibrating = false;
                    
                    ESP_LOGI(TAG, "=== CALIBRATION COMPLETE ===");
                    ESP_LOGI(TAG, "New Hard-Iron Center -> X:%.1f | Y:%.1f | Z:%.1f", 
                             mag_profile.offset_x, mag_profile.offset_y, mag_profile.offset_z);
                }
                
                // Skip the ESKF math and UI updates while collecting data
                continue; 
            }

            // ARCHITECT FIX: Dynamic NVS Hard-Iron Compensation"""

cpp_content = cpp_content.replace("            // ARCHITECT FIX: Dynamic NVS Hard-Iron Compensation", calib_logic)

with open(fusion_cpp, "w") as f:
    f.write(cpp_content)

print("Successfully injected the Dynamic Field Calibration State Machine!")