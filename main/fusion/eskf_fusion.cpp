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

    // Baseline Earth field norm for the Spherical Shield
    static float pristine_norm = 0.0f;

    ESP_LOGI(TAG, "Advanced Physics Engine Task Started (Core 1).");

    while (1) {
        if (xQueueReceive(imu_queue, &sensor_data, portMAX_DELAY) == pdTRUE) {
            current_time = dsp_get_cpu_cycle_count();
            if (current_time > prev_time) {
                dt = (current_time - prev_time) / 160000000.0;
            }
            prev_time = current_time;

            if (is_calibrating && sensor_data.mag_valid) {
                if (sensor_data.mag_x < mag_min[0]) mag_min[0] = sensor_data.mag_x;
                if (sensor_data.mag_x > mag_max[0]) mag_max[0] = sensor_data.mag_x;
                
                if (sensor_data.mag_y < mag_min[1]) mag_min[1] = sensor_data.mag_y;
                if (sensor_data.mag_y > mag_max[1]) mag_max[1] = sensor_data.mag_y;
                
                if (sensor_data.mag_z < mag_min[2]) mag_min[2] = sensor_data.mag_z;
                if (sensor_data.mag_z > mag_max[2]) mag_max[2] = sensor_data.mag_z;
                
                calib_samples++;
                
                if (calib_samples >= 900) {
                    mag_profile.offset_x = (mag_max[0] + mag_min[0]) / 2.0f;
                    mag_profile.offset_y = (mag_max[1] + mag_min[1]) / 2.0f;
                    mag_profile.offset_z = (mag_max[2] + mag_min[2]) / 2.0f;
                    mag_profile.is_calibrated = true;
                    
                    eskf_save_calibration(&mag_profile);
                    is_calibrating = false;
                    
                    pristine_norm = 0.0f; // Force a re-latch of the new geographical sphere
                    ekf13->Init(); 
                    ESP_LOGI(TAG, "=== CALIBRATION COMPLETE ===");
                }
                continue; 
            }

            // ====================================================================
            // 1. STANDARD HARD-IRON CALIBRATION
            // ====================================================================
            float hx = sensor_data.mag_x;
            float hy = sensor_data.mag_y;
            float hz = sensor_data.mag_z;

            if (mag_profile.is_calibrated) {
                hx -= mag_profile.offset_x;
                hy -= mag_profile.offset_y;
                hz -= mag_profile.offset_z;
            }

            // ARCHITECT FIX: Restored the missing telemetry logging block
            static uint32_t telemetry_counter = 0;
            if (telemetry_counter++ % 50 == 0) {
                ESP_LOGI(TAG, "--- ESKF INPUT (CALIBRATED XYZ) ---");
                ESP_LOGI(TAG, "ACC | X: %7.0f | Y: %7.0f | Z: %7.0f", sensor_data.acc_x, sensor_data.acc_y, sensor_data.acc_z);
                ESP_LOGI(TAG, "GYR | X: %7.0f | Y: %7.0f | Z: %7.0f", sensor_data.gyr_x, sensor_data.gyr_y, sensor_data.gyr_z);
                ESP_LOGI(TAG, "MAG | X: %7.0f | Y: %7.0f | Z: %7.0f", hx, hy, hz);
                ESP_LOGI(TAG, "-------------------------------");
            }

            // Load back into arrays for the DSP Matrix
            float acc_arr[3] = {sensor_data.acc_x, sensor_data.acc_y, sensor_data.acc_z};
            float gyr_arr[3] = {sensor_data.gyr_x, sensor_data.gyr_y, sensor_data.gyr_z};
            float mag_arr[3] = {hx, hy, hz};

            dspm::Mat gyro_input_mat(gyr_arr, 3, 1);
            dspm::Mat accel_input_mat(acc_arr, 3, 1);
            dspm::Mat mag_input_mat(mag_arr, 3, 1);

            accel_input_mat = accel_input_mat / 32768.0f * 16.0f;
            gyro_input_mat *= (2000.0f * DEG_TO_RAD / 32768.0f);

            // ====================================================================
            // 2. DYNAMIC COVARIANCE SCALING (The Aerospace Leash)
            // ====================================================================
            float current_mag_norm = mag_input_mat.norm();
            
            // Latch the pristine Earth field norm directly after boot/calibration
            if (pristine_norm == 0.0f && mag_profile.is_calibrated && current_mag_norm > 10.0f) {
                pristine_norm = current_mag_norm;
                ESP_LOGI(TAG, "Latched Pristine Magnetic Radius: %.1f uT", pristine_norm);
            }

            float spherical_distortion = 0.0f;
            if (pristine_norm > 0.0f) {
                spherical_distortion = fabsf(current_mag_norm - pristine_norm) / pristine_norm;
                
                // Micro-adaptation for regional geographic drift over long periods
                if (spherical_distortion < 0.05f) {
                    pristine_norm = (pristine_norm * 0.9999f) + (current_mag_norm * 0.0001f);
                }
            }

            // Safe Normalization for ESKF ingestion
            if (current_mag_norm > 0.001f) {
                mag_input_mat = (1.0f / current_mag_norm) * mag_input_mat;
            }

            // Step 1: Execute Free Integration of High-Frequency Kinematics
            ekf13->Process(gyro_input_mat.data, dt);

            // ====================================================================
            // 3. CONTINUOUS SOFT-GATING UPDATE
            // ====================================================================
            if (sensor_data.mag_valid) {
                // Base Variances. We elevate the base mag variance slightly from 0.03 
                // to 0.10 to permanently widen the Mahalanobis gate, preventing the 
                // "Arrogance Lockout" after long walks without hacking the P-Matrix.
                float acc_var = 0.5f;
                float mag_var = 0.10f; 

                // Dynamic Inflation: If the sphere distorts (walking past metal), 
                // we exponentially inflate the measurement variance. This tells the EKF 
                // to natively trust the gyro and ignore the bad mag data smoothly,
                // eliminating the violent "jumping" caused by binary ON/OFF logic.
                if (spherical_distortion > 0.05f) {
                    mag_var += (spherical_distortion * 15.0f); 
                }

                float R_m[6] = {acc_var, acc_var, acc_var, mag_var, mag_var, mag_var}; 
                ekf13->UpdateRefMeasurementMagn(accel_input_mat.data, mag_input_mat.data, R_m);
            }

            // Extract post-update Quaternions for LVGL Graphics Engine
            current_q.q0 = ekf13->X.data[0];
            current_q.q1 = ekf13->X.data[1];
            current_q.q2 = ekf13->X.data[2];
            current_q.q3 = ekf13->X.data[3];

            ui_render_update_3d(&current_q, !sensor_data.mag_valid);
        }
    }
}

void eskf_fusion_init(void) {
    ESP_LOGI(TAG, "Initializing ESKF Physics Engine...");
    ekf13 = new ekf_imu13states();
    ekf13->Init();

    if (eskf_load_calibration(&mag_profile) != ESP_OK || !mag_profile.is_calibrated) {
        // Restored your original native fallbacks
        mag_profile.offset_x = -143.5f;
        mag_profile.offset_y = 85.0f;
        mag_profile.offset_z = 325.0f;
        mag_profile.is_calibrated = true; 
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
        mag_min[0] = 99999.0f; mag_min[1] = 99999.0f; mag_min[2] = 99999.0f;
        mag_max[0] = -99999.0f; mag_max[1] = -99999.0f; mag_max[2] = -99999.0f;
        calib_samples = 0;
        is_calibrating = true;
    }
}

bool eskf_is_calibrating(void) {
    return is_calibrating;
}
