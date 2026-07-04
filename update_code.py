import os
import re

app_main_path = os.path.join("main", "core", "app_main.cpp")

with open(app_main_path, "r") as f:
    content = f.read()

# ARCHITECT FIX: Amputate Stage 2 and replace with a pure 9D Stage 1 tracker
pattern = re.compile(r"// ARCHITECT FIX: Stage 2 Pipeline Isolation.*?dspm::Mat mag_input_mat\(calib\.mag, 3, 1\);", re.DOTALL)

stage1_9d_integration = """// ARCHITECT FIX: Stage 1 Pipeline Isolation (9D Verification)
        BodyVectors body = stage1_hal_transform((int16_t*)sensors_data);

        static uint32_t stage1_telemetry = 0;
        if (stage1_telemetry++ % 50 == 0) {
            ESP_LOGI(TAG, "--- STAGE 1: 9D BODY ENU RAW VERIFICATION ---");
            ESP_LOGI(TAG, "ACCEL | X: %6.0f | Y: %6.0f | Z: %6.0f", body.accel[0], body.accel[1], body.accel[2]);
            ESP_LOGI(TAG, "GYRO  | X: %6.0f | Y: %6.0f | Z: %6.0f", body.gyro[0], body.gyro[1], body.gyro[2]);
            ESP_LOGI(TAG, "MAGN  | X: %6.0f | Y: %6.0f | Z: %6.0f", body.mag[0], body.mag[1], body.mag[2]);
            ESP_LOGI(TAG, "---------------------------------------------");
        }

        // Dummy feed to keep the EKF from crashing while we do HAL testing.
        // We do not care about the EKF output or the screen right now.
        dspm::Mat gyro_input_mat(body.gyro, 3, 1);
        dspm::Mat accel_input_mat(body.accel, 3, 1);
        dspm::Mat mag_input_mat(body.mag, 3, 1);"""

content, count = pattern.subn(stage1_9d_integration, content)

if count > 0:
    with open(app_main_path, "w") as f:
        f.write(content)
    print(f"Successfully patched {app_main_path} for 9D Stage 1 testing.")
else:
    print("Error: Target code block not found using regex.")