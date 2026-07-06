#include "eskf_fusion.h"
#include "nvs_calibration.h"
#include "ui_render.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
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
    static int64_t prev_time = 0;
    int64_t current_time_us = esp_timer_get_time();

    // Baseline Earth field norm for the Spherical Shield
    static float pristine_norm = 0.0f;

    ESP_LOGI(TAG, "Advanced Physics Engine Task Started (Core 1).");

    while (1) {
        if (xQueueReceive(imu_queue, &sensor_data, portMAX_DELAY) == pdTRUE) {
            current_time_us = esp_timer_get_time();
            if (prev_time != 0) {
                dt = (float)(current_time_us - prev_time) / 1000000.0f;
                if (dt <= 0.0f || dt > 0.1f) dt = 0.01f;
            }
            prev_time = current_time_us;

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

            
            // ====================================================================
            // 4. STRAPDOWN KINEMATICS (High-Pass Velocity & Positional Washout)
            // ====================================================================
            static float vel_ned[3] = {0.0f, 0.0f, 0.0f};
            static float pos_ned[3] = {0.0f, 0.0f, 0.0f};

            float q0 = current_q.q0, q1 = current_q.q1, q2 = current_q.q2, q3 = current_q.q3;
            float r00 = 1.0f - 2.0f * (q2*q2 + q3*q3);
            float r01 = 2.0f * (q1*q2 - q0*q3);
            float r02 = 2.0f * (q1*q3 + q0*q2);
            float r10 = 2.0f * (q1*q2 + q0*q3);
            float r11 = 1.0f - 2.0f * (q1*q1 + q3*q3);
            float r12 = 2.0f * (q2*q3 - q0*q1);
            float r20 = 2.0f * (q1*q3 - q0*q2);
            float r21 = 2.0f * (q2*q3 + q0*q1);
            float r22 = 1.0f - 2.0f * (q1*q1 + q2*q2);

            float raw_ax = sensor_data.acc_x / 32768.0f * 16.0f;
            float raw_ay = sensor_data.acc_y / 32768.0f * 16.0f;
            float raw_az = sensor_data.acc_z / 32768.0f * 16.0f;

            float raw_gx = sensor_data.gyr_x * (2000.0f / 32768.0f);
            float raw_gy = sensor_data.gyr_y * (2000.0f / 32768.0f);
            float raw_gz = sensor_data.gyr_z * (2000.0f / 32768.0f);

            float ax_earth = r00 * raw_ax + r01 * raw_ay + r02 * raw_az;
            float ay_earth = r10 * raw_ax + r11 * raw_ay + r12 * raw_az;
            float az_earth = r20 * raw_ax + r21 * raw_ay + r22 * raw_az;

            // Evaluate physical movement strictly in the Body Frame
            float raw_acc_norm = sqrtf(raw_ax*raw_ax + raw_ay*raw_ay + raw_az*raw_az);
            float raw_gyr_norm = sqrtf(raw_gx*raw_gx + raw_gy*raw_gy + raw_gz*raw_gz);

            // Device is stationary if acceleration is ~1.0G and gyro is quiet
            bool is_stationary = (raw_acc_norm > 0.5f) && (fabsf(raw_acc_norm - 1.0f) < 0.05f) && (raw_gyr_norm < 3.0f);

            static float gx = 0, gy = 0, gz = 0;
            static bool g_init = false;
            static float warmup_timer = 0.0f;
            
            float a_kin_x = 0, a_kin_y = 0, a_kin_z = 0;

            if (dt > 0) {
                // 1. Boot-Time Grace Period (2.0 Seconds)
                if (warmup_timer < 2.0f) {
                    warmup_timer += dt;
                    if (is_stationary) {
                        gx = ax_earth; gy = ay_earth; gz = az_earth;
                        g_init = true;
                    }
                    vel_ned[0] = 0; vel_ned[1] = 0; vel_ned[2] = 0;
                    pos_ned[0] = 0; pos_ned[1] = 0; pos_ned[2] = 0;
                } else {
                    // 2. Standard Operation
                    if (is_stationary && g_init) {
                        // DEVICE RESTING: Deploy the Positional Washout and Hard ZUPT Hammer
                        
                        // Hard zero the velocity immediately (The Instant ZUPT Hammer)
                        vel_ned[0] = 0.0f; 
                        vel_ned[1] = 0.0f; 
                        vel_ned[2] = 0.0f;

                        // The Positional Washout (Rubber Band): 
                        // Without GPS, IMUs can't know absolute position. This smoothly reels the 
                        // phantom drift back to 0.0 cm so the HUD is pristine for the next move.
                        float spring_force = dt * 2.0f; // Snaps back over ~0.5 seconds
                        if (spring_force > 1.0f) spring_force = 1.0f;
                        pos_ned[0] -= pos_ned[0] * spring_force;
                        pos_ned[1] -= pos_ned[1] * spring_force;
                        pos_ned[2] -= pos_ned[2] * spring_force;

                        // Hard clamp exact zeros for clean UI when settled
                        if (fabsf(pos_ned[0]) < 0.05f) pos_ned[0] = 0.0f;
                        if (fabsf(pos_ned[1]) < 0.05f) pos_ned[1] = 0.0f;
                        if (fabsf(pos_ned[2]) < 0.05f) pos_ned[2] = 0.0f;

                        // Absorb residual Earth-frame gravity
                        float alpha = dt / 0.5f; 
                        if (alpha > 1.0f) alpha = 1.0f;
                        gx += (ax_earth - gx) * alpha;
                        gy += (ay_earth - gy) * alpha;
                        gz += (az_earth - gz) * alpha;

                    } else if (g_init) {
                        // DEVICE MOVING: Shield baseline and integrate kinetics
                        a_kin_x = (ax_earth - gx) * 9.80665f;
                        a_kin_y = (ay_earth - gy) * 9.80665f;
                        a_kin_z = (az_earth - gz) * 9.80665f;

                        // 0.20 m/s^2 deadband to ignore microscopic noise while hand-held
                        float kin_norm = sqrtf(a_kin_x*a_kin_x + a_kin_y*a_kin_y + a_kin_z*a_kin_z);
                        if (kin_norm < 0.20f) {
                            a_kin_x = 0; a_kin_y = 0; a_kin_z = 0;
                        }

                        // THE HIGH-PASS VELOCITY FILTER
                        // We aggressively leak the velocity back toward zero even while moving. 
                        // This mathematically limits the unbounded gravity-bleed accumulation 
                        // when the device is pitched or rolled rapidly.
                        float leak_rate = 1.0f - (1.5f * dt); 
                        if (leak_rate < 0.0f) leak_rate = 0.0f;
                        
                        vel_ned[0] = (vel_ned[0] * leak_rate) + (a_kin_x * dt);
                        vel_ned[1] = (vel_ned[1] * leak_rate) + (a_kin_y * dt);
                        vel_ned[2] = (vel_ned[2] * leak_rate) + (a_kin_z * dt);

                        // Integrate position natively
                        pos_ned[0] += vel_ned[0] * dt;
                        pos_ned[1] += vel_ned[1] * dt;
                        pos_ned[2] += vel_ned[2] * dt;
                    }
                }
            }

            // --- TELEMETRY LOGGING ---
            static uint32_t kin_debug = 0;
            if (kin_debug++ % 20 == 0) {
                if (warmup_timer < 2.0f) {
                    ESP_LOGI(TAG, "WARMUP | stat: %d | raw_acc: %.3f G | Time: %.1f/2.0s", is_stationary, raw_acc_norm, warmup_timer);
                } else {
                    ESP_LOGI(TAG, "KINEMATICS | stat: %d | dt: %.4f | V: %5.1f %5.1f %5.1f | P: %5.1f %5.1f %5.1f", 
                             is_stationary, dt, 
                             vel_ned[0]*100.0f, vel_ned[1]*100.0f, vel_ned[2]*100.0f,
                             pos_ned[0]*100.0f, pos_ned[1]*100.0f, pos_ned[2]*100.0f);
                }
            }

            // Throttle LVGL graphics ping to ~50Hz to prevent Mutex Starvation
            static uint8_t render_throttle = 0;
            if (render_throttle++ % 4 == 0) { 
                ui_render_update_3d(&current_q, !sensor_data.mag_valid, vel_ned, pos_ned);
            }
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
