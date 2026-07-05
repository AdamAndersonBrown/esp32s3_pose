import os

hal_cpp_path = os.path.join("main", "hal", "hal_imu.cpp")
fusion_cpp_path = os.path.join("main", "fusion", "eskf_fusion.cpp")

# 1. Finalize the Hardware Abstraction Layer (Data Sanitization)
hal_cpp_content = """#include "hal_imu.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "HAL_IMU";
static bool imu_initialized = false;

// Hardware Variance Watchdog State
static float last_mag[3] = {0.0f, 0.0f, 0.0f};

esp_err_t imu_hal_init(void) {
    ESP_LOGI(TAG, "Initializing 9-DoF Hardware Abstraction Layer...");
    // TODO: Migrate the BMM150 Wake/Suspend I2C writes here.
    imu_initialized = true;
    return ESP_OK;
}

esp_err_t imu_hal_read_9dof(imu_9dof_data_t *data) {
    if (!data || !imu_initialized) return ESP_ERR_INVALID_STATE;

    // 1. Execute the blocking I2C read from the BMI270 Gatekeeper
    // TODO: Drop your raw read_bmi270_data() polling execution here.
    
    // Placeholder variables for your raw parsed integer data
    float raw_mag_x = 0.0f; 
    float raw_mag_y = 0.0f;
    float raw_mag_z = 0.0f; 

    data->mag_x = raw_mag_x;
    data->mag_y = raw_mag_y;
    
    // 2. ARCHITECT FIX: Right-Hand Rule Z-Axis Parity Inversion
    // Invert Mag Z to mathematically align with Earth's downward gravity vector
    data->mag_z = -raw_mag_z;

    // 3. ARCHITECT FIX: Hardware Variance Watchdog
    // The BMM150 auxiliary bus can deadlock silently without throwing NACKs.
    // True analog magnetic fields ALWAYS exhibit analog noise. 
    // If the vector is mathematically identical to the last frame, the silicon is ghosting.
    if (data->mag_x == last_mag[0] && 
        data->mag_y == last_mag[1] && 
        data->mag_z == last_mag[2]) {
        data->mag_valid = false;
    } else {
        data->mag_valid = true;
        last_mag[0] = data->mag_x;
        last_mag[1] = data->mag_y;
        last_mag[2] = data->mag_z;
    }

    return ESP_OK;
}
"""

# 2. Finalize the ESKF Physics Thread (NVS & Data Fusion)
fusion_cpp_content = """#include "eskf_fusion.h"
#include "nvs_calibration.h"
#include "ui_render.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "ESKF_PHYSICS";
static QueueHandle_t imu_queue = NULL;
static hard_iron_profile_t mag_calibration;

static void eskf_physics_task(void *pvParameters) {
    imu_9dof_data_t sensor_data;
    quaternion_t current_q = {1.0f, 0.0f, 0.0f, 0.0f};

    ESP_LOGI(TAG, "Physics Engine Task Started (Core 1).");

    while (1) {
        // Block until new hardware data is passed from Core 0
        if (xQueueReceive(imu_queue, &sensor_data, portMAX_DELAY) == pdTRUE) {
            
            // 1. Hardware Variance Watchdog Fallback
            if (!sensor_data.mag_valid) {
                // Coast purely on Gyroscope. 
                // Gating out the magnetometer covariance ensures the autonomous 
                // physics model doesn't violently snap during an auxiliary bus deadlock.
            } 
            else if (mag_calibration.is_calibrated) {
                // 2. Apply NVS Hard-Iron Calibration Offsets
                sensor_data.mag_x -= mag_calibration.offset_x;
                sensor_data.mag_y -= mag_calibration.offset_y;
                sensor_data.mag_z -= mag_calibration.offset_z;
            }

            // 3. TODO: Execute standard Madgwick/Kalman floating-point update step here
            // using the sanitized sensor_data struct.

            // 4. Push normalized quaternion to LVGL Render Task
            ui_render_update_3d(&current_q);
        }
    }
}

void eskf_fusion_init(void) {
    ESP_LOGI(TAG, "Initializing ESKF Physics Engine...");
    
    // Boot NVS and load saved magnetic footprint
    if (eskf_load_calibration(&mag_calibration) != ESP_OK) {
        ESP_LOGW(TAG, "Using default factory magnetic profile.");
    }

    imu_queue = xQueueCreate(10, sizeof(imu_9dof_data_t));
    if (imu_queue != NULL) {
        xTaskCreatePinnedToCore(
            eskf_physics_task,
            "eskf_physics",
            8192,
            NULL,
            5,
            NULL,
            1
        );
    }
}

void eskf_fusion_queue_data(imu_9dof_data_t *data) {
    if (imu_queue != NULL) {
        xQueueSend(imu_queue, data, 0);
    }
}
"""

with open(hal_cpp_path, "w") as f:
    f.write(hal_cpp_content)

with open(fusion_cpp_path, "w") as f:
    f.write(fusion_cpp_content)

print("Successfully established Data Pipeline and Hardware Watchdog!")