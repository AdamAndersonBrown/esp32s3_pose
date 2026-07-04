import os
import re

app_main_path = os.path.join("main", "core", "app_main.cpp")

with open(app_main_path, "r") as f:
    content = f.read()

# Locate the entire transformation and ESKF input block
pattern = re.compile(
    r"BodyVectors body = stage1_hal_transform\(\(int16_t\*\)sensors_data\);.*?dspm::Mat mag_input_mat\([^\)]+\);",
    re.DOTALL
)

# Inject the direct hardware-to-ESKF pipeline
injection = """BodyVectors body = stage1_hal_transform((int16_t*)sensors_data);
        
        // ARCHITECT FIX: Direct Hardware-to-ESKF Pipeline
        // Bypassing redundant FRD/NED abstractions. 
        // Applying mathematically proven silicon offsets to the magnetometer.
        float calib_mag[3];
        calib_mag[0] = body.mag[0] - (-143.5f);
        calib_mag[1] = body.mag[1] - (85.0f);
        calib_mag[2] = body.mag[2] - (325.0f);

        static uint32_t telemetry_counter = 0;
        if (telemetry_counter++ % 50 == 0) {
            ESP_LOGI(TAG, "--- ESKF INPUT (NATIVE XYZ) ---");
            ESP_LOGI(TAG, "ACC | X: %7.0f | Y: %7.0f | Z: %7.0f", body.accel[0], body.accel[1], body.accel[2]);
            ESP_LOGI(TAG, "GYR | X: %7.0f | Y: %7.0f | Z: %7.0f", body.gyro[0], body.gyro[1], body.gyro[2]);
            ESP_LOGI(TAG, "MAG | X: %7.0f | Y: %7.0f | Z: %7.0f", calib_mag[0], calib_mag[1], calib_mag[2]);
            ESP_LOGI(TAG, "-------------------------------");
        }

        // Feed the native, right-handed XYZ vectors directly into the ESKF Matrix Engine
        dspm::Mat gyro_input_mat(body.gyro, 3, 1);
        dspm::Mat accel_input_mat(body.accel, 3, 1);
        dspm::Mat mag_input_mat(calib_mag, 3, 1);"""

content, count = pattern.subn(injection, content)

if count > 0:
    with open(app_main_path, "w") as f:
        f.write(content)
    print("Successfully wired native XYZ arrays directly into the ESKF.")
else:
    print("Error: Could not find the transformation block to replace.")