import os

fusion_h = os.path.join("main", "fusion", "eskf_fusion.h")
fusion_cpp = os.path.join("main", "fusion", "eskf_fusion.cpp")

fusion_h_content = """#pragma once
#include "hal_imu.h"

// STRICT DATA STRUCTURE:
// The unified output of the physics engine.
typedef struct {
    float q0, q1, q2, q3;
} quaternion_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the ESKF Physics Engine and spawns the FPU thread.
 */
void eskf_fusion_init(void);

/**
 * @brief Non-blocking function to push raw I2C data into the Physics Queue.
 * @param data The raw telemetry from the HAL.
 */
void eskf_fusion_queue_data(imu_9dof_data_t *data);

#ifdef __cplusplus
}
#endif
"""

fusion_cpp_content = """#include "eskf_fusion.h"
#include "ui_render.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "ESKF_FUSION";

// The RTOS Queue for thread isolation (Core 0 -> Core 1)
static QueueHandle_t imu_queue = NULL;

// The dedicated FPU Physics Task
static void eskf_physics_task(void *pvParameters) {
    imu_9dof_data_t sensor_data;
    quaternion_t current_q = {1.0f, 0.0f, 0.0f, 0.0f};

    ESP_LOGI(TAG, "Physics Engine Thread started on Core 1 (FPU Isolated).");

    while (1) {
        // Block with zero CPU overhead until new sensor data arrives
        if (xQueueReceive(imu_queue, &sensor_data, portMAX_DELAY) == pdTRUE) {
            
            // TODO: Migrate the Kalman/Madgwick floating-point math here.
            // This thread is now completely immune to I2C bus latency.
            
            // Push the calculated quaternion to the LVGL thread
            ui_render_update_3d(&current_q);
        }
    }
}

void eskf_fusion_init(void) {
    ESP_LOGI(TAG, "Initializing Physics Threading...");
    
    // Create a queue capable of buffering 10 sensor frames
    imu_queue = xQueueCreate(10, sizeof(imu_9dof_data_t));
    
    if (imu_queue != NULL) {
        // Pin the physics engine to Core 1 to prevent FPU thrashing.
        // Priority 5 ensures it preempts background tasks for zero-latency math.
        xTaskCreatePinnedToCore(
            eskf_physics_task,   // Task function
            "eskf_physics",      // Name
            8192,                // Stack size (8KB for FPU matrix math)
            NULL,                // Parameters
            5,                   // Priority
            NULL,                // Task handle
            1                    // Core 1 (APP_CPU)
        );
    } else {
        ESP_LOGE(TAG, "Failed to create IMU FreeRTOS Queue.");
    }
}

void eskf_fusion_queue_data(imu_9dof_data_t *data) {
    if (imu_queue != NULL) {
        // Non-blocking send from the I2C polling loop
        xQueueSend(imu_queue, data, 0);
    }
}
"""

with open(fusion_h, "w") as f:
    f.write(fusion_h_content)

with open(fusion_cpp, "w") as f:
    f.write(fusion_cpp_content)

print("Successfully established the threaded ESKF Physics Engine!")