#include "eskf_fusion.h"
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
