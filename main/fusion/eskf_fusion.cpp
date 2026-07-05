#include "eskf_fusion.h"
#include "nvs_calibration.h"
#include "ui_render.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <math.h>
#include "sensor_hal.h"
#include "cube_matrix.h"
#include "image_to_3d_matrix.h"

// Failsafe definitions in case the BSP math headers are aggressively pruned
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef DEG_TO_RAD
#define DEG_TO_RAD (M_PI / 180.0f)
#endif

#include "esp_dsp.h"
#include "sensor_calib.h"
#include "sensor_ned.h"

static const char *TAG = "ESKF_PHYSICS";
static QueueHandle_t imu_queue = NULL;
static ekf_imu13states *ekf13 = NULL;

static void eskf_physics_task(void *pvParameters) {
    imu_9dof_data_t sensor_data;
    quaternion_t current_q = {1.0f, 0.0f, 0.0f, 0.0f};

    float dt = 0;
    static float prev_time = 0;
    float current_time = dsp_get_cpu_cycle_count();
    float R_m[6] = {0.5f, 0.5f, 0.5f, 0.03f, 0.03f, 0.03f};

    ESP_LOGI(TAG, "Physics Engine Task Started (Core 1).");

    while (1) {
        if (xQueueReceive(imu_queue, &sensor_data, portMAX_DELAY) == pdTRUE) {
            current_time = dsp_get_cpu_cycle_count();
            if (current_time > prev_time) {
                dt = (current_time - prev_time) / 160000000.0;
            }
            prev_time = current_time;

            // Apply specific hard-iron chassis offsets
            sensor_data.mag_x -= (-143.5f);
            sensor_data.mag_y -= (85.0f);
            sensor_data.mag_z -= (325.0f);

            static uint32_t telemetry_counter = 0;
            if (telemetry_counter++ % 50 == 0) {
                ESP_LOGI(TAG, "--- ESKF INPUT (NATIVE XYZ) ---");
                ESP_LOGI(TAG, "ACC | X: %7.0f | Y: %7.0f | Z: %7.0f", sensor_data.acc_x, sensor_data.acc_y, sensor_data.acc_z);
                ESP_LOGI(TAG, "GYR | X: %7.0f | Y: %7.0f | Z: %7.0f", sensor_data.gyr_x, sensor_data.gyr_y, sensor_data.gyr_z);
                ESP_LOGI(TAG, "MAG | X: %7.0f | Y: %7.0f | Z: %7.0f", sensor_data.mag_x, sensor_data.mag_y, sensor_data.mag_z);
                ESP_LOGI(TAG, "-------------------------------");
            }

            float acc_arr[3] = {sensor_data.acc_x, sensor_data.acc_y, sensor_data.acc_z};
            float gyr_arr[3] = {sensor_data.gyr_x, sensor_data.gyr_y, sensor_data.gyr_z};
            float mag_arr[3] = {sensor_data.mag_x, sensor_data.mag_y, sensor_data.mag_z};

            dspm::Mat gyro_input_mat(gyr_arr, 3, 1);
            dspm::Mat accel_input_mat(acc_arr, 3, 1);
            dspm::Mat mag_input_mat(mag_arr, 3, 1);

            accel_input_mat = accel_input_mat / 32768.0f * 16.0f;
            
            float current_norm = mag_input_mat.norm();
            if (current_norm > 0.001f) {
                mag_input_mat = (1.0f / current_norm) * mag_input_mat;
            }

            gyro_input_mat *= (2000.0f * DEG_TO_RAD / 32768.0f);

            ekf13->Process(gyro_input_mat.data, dt);
            
            if (sensor_data.mag_valid) {
                ekf13->UpdateRefMeasurementMagn(accel_input_mat.data, mag_input_mat.data, R_m);
            }

            current_q.q0 = ekf13->X.data[0];
            current_q.q1 = ekf13->X.data[1];
            current_q.q2 = ekf13->X.data[2];
            current_q.q3 = ekf13->X.data[3];

            static uint8_t diag_tick = 0;
            if (++diag_tick % 50 == 0) {
                float raw_yaw = atan2(sensor_data.mag_y, sensor_data.mag_x) * (180.0f / M_PI);
                float q0=current_q.q0, q1=current_q.q1, q2=current_q.q2, q3=current_q.q3;
                float ekf_yaw = atan2(2.0f * (q0 * q3 + q1 * q2), 1.0f - 2.0f * (q2 * q2 + q3 * q3)) * (180.0f / M_PI);
                ESP_LOGW(TAG, "DIAGNOSTIC -> Raw Mag Yaw: %0.1f deg | EKF Yaw: %0.1f deg", raw_yaw, ekf_yaw);
            }

            ui_render_update_3d(&current_q, !sensor_data.mag_valid);
        }
    }
}

void eskf_fusion_init(void) {
    ESP_LOGI(TAG, "Initializing ESKF Physics Engine...");
    
    ekf13 = new ekf_imu13states();
    ekf13->Init();

    imu_queue = xQueueCreate(10, sizeof(imu_9dof_data_t));
    if (imu_queue != NULL) {
        xTaskCreatePinnedToCore(eskf_physics_task, "eskf_physics", 16384, NULL, 5, NULL, 1);
    }
}

void eskf_fusion_queue_data(imu_9dof_data_t *data) {
    if (imu_queue != NULL) {
        xQueueSend(imu_queue, data, 0);
    }
}
