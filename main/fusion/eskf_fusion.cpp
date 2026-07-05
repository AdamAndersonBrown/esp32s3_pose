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
static hard_iron_profile_t mag_profile;

// Calibration State Machine
static bool is_calibrating = false;
static uint16_t calib_samples = 0;
static float mag_min[3] = {99999.0f, 99999.0f, 99999.0f};
static float mag_max[3] = {-99999.0f, -99999.0f, -99999.0f};

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

            // ARCHITECT FIX: Dynamic Field Calibration Interceptor
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

            // ARCHITECT FIX: Dynamic NVS Hard-Iron Compensation
            if (mag_profile.is_calibrated) {
                sensor_data.mag_x -= mag_profile.offset_x;
                sensor_data.mag_y -= mag_profile.offset_y;
                sensor_data.mag_z -= mag_profile.offset_z;
            }

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

    // ARCHITECT FIX: Load localized Hard-Iron footprint from Flash Memory
    if (eskf_load_calibration(&mag_profile) != ESP_OK || !mag_profile.is_calibrated) {
        ESP_LOGW(TAG, "NVS empty. Provisioning experimental baseline to physical flash memory...");
        mag_profile.offset_x = -143.5f;
        mag_profile.offset_y = 85.0f;
        mag_profile.offset_z = 325.0f;
        mag_profile.is_calibrated = true; 
        
        // Committing the values to NVS so future boots load from storage, not source code
        eskf_save_calibration(&mag_profile);
    }

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
