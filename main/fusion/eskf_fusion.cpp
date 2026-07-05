#include "eskf_fusion.h"
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
