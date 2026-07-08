#include "driver/i2c.h"
#include "bsp/m5stack_core_s3.h"
#include "eskf_fusion.h"
#include "physics_constants.h"
#include "calibration.h"
#include "kinematics.h"
#include "odometry.h"
#include "ui_render.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include "cube_matrix.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef DEG_TO_RAD
#define DEG_TO_RAD (M_PI / 180.0f)
#endif

#include "esp_dsp.h"

static const char *TAG = "ESKF_PHYSICS";
static portMUX_TYPE state_spinlock = portMUX_INITIALIZER_UNLOCKED;
static eskf_state_t global_state;
static QueueHandle_t imu_queue = NULL;
static ekf_imu13states *ekf13 = NULL;


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

            if (calibration_is_active() && sensor_data.mag_valid) {
                if (calibration_process_sample(sensor_data.mag_x, sensor_data.mag_y, sensor_data.mag_z)) {
                    pristine_norm = 0.0f; // Force a re-latch of the new geographical sphere
                    ekf13->Init(); 
                    ESP_LOGI(TAG, "=== CALIBRATION COMPLETE ===");
                }
                continue; 
            }

            // ====================================================================
            // 0.5 SILICON HARDWARE BIAS COMPENSATION
            // Derived from static telemetry averages
            sensor_data.gyr_x -= -0.04f;
            sensor_data.gyr_y -= 2.24f;
            sensor_data.gyr_z -= -1.79f;

            // 1. STANDARD HARD-IRON CALIBRATION
            // ====================================================================
            float hx = sensor_data.mag_x;
            float hy = sensor_data.mag_y;
            float hz = sensor_data.mag_z;

            calibration_apply(&hx, &hy, &hz);

            // ARCHITECT FIX: Pure CSV Telemetry Stream for R Matrix Extraction
            static uint32_t telemetry_counter = 0;
            if (telemetry_counter++ % 50 == 0) {
                // Print the CSV header on the very first frame
                if (telemetry_counter == 1) {
                    printf("AccX,AccY,AccZ,GyrX,GyrY,GyrZ,MagX,MagY,MagZ\n");
                }
                // Stream pristine, comma-separated hardware telemetry
                printf("%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f\n", 
                       sensor_data.acc_x, sensor_data.acc_y, sensor_data.acc_z,
                       sensor_data.gyr_x, sensor_data.gyr_y, sensor_data.gyr_z,
                       hx, hy, hz);
            }

            // Load arrays natively in Body ENU Frame.
            // The LVGL projection matrix inherently expects ENU alignment.
            // Note: The raw silicon Z-axis is already inverted inside hal_imu.cpp
            // to satisfy the ESKF Right-Hand Rule parity.
            float acc_arr[3] = {sensor_data.acc_x, sensor_data.acc_y, sensor_data.acc_z};
            float gyr_arr[3] = {sensor_data.gyr_x, sensor_data.gyr_y, sensor_data.gyr_z};
            float mag_arr[3] = {hx, hy, hz};

            dspm::Mat gyro_input_mat(gyr_arr, 3, 1);
            dspm::Mat accel_input_mat(acc_arr, 3, 1);
            dspm::Mat mag_input_mat(mag_arr, 3, 1);

            accel_input_mat = accel_input_mat * IMU_ACCEL_SCALE_16G;
            gyro_input_mat *= (IMU_GYRO_SCALE_2000DPS * DEG_TO_RAD);

            // ====================================================================
            // 2. DYNAMIC COVARIANCE SCALING (The Aerospace Leash)
            // ====================================================================
            float current_mag_norm = mag_input_mat.norm();
            
            // Latch the pristine Earth field norm directly after boot/calibration
            if (pristine_norm == 0.0f && current_mag_norm > 10.0f) {
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
                // Standard Covariance Tuning per ESKF Physics Requirements
                float acc_var = 0.5f;   // Detuned to reject high-frequency kinetic noise
                float mag_var = 0.03f;  // Normalized unit-vector variance for a gentle yaw leash
                
                // Dynamic R-Matrix inflation is strictly prohibited.
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
            static float a_kin[3] = {0.0f, 0.0f, 0.0f};
            static float pure_pos[3] = {0.0f, 0.0f, 0.0f};
            bool is_moving = false;
            bool clear_hold = false;
            
            kinematics_process(dt, &sensor_data, &current_q, vel_ned, pos_ned, &is_moving, a_kin, &clear_hold);
            odometry_process(dt, a_kin, is_moving, clear_hold, pure_pos);
            
            

            // --- TELEMETRY LOGGING ---
            static uint32_t kin_debug = 0;
            if (kin_debug++ % 20 == 0) {
                // ARCHITECT FIX: Temporarily disabled to prevent CSV stream corruption during R-Matrix extraction
                /*
                if (warmup_timer < 2.0f) {
                    ESP_LOGI(TAG, "WARMUP | stat: %d | raw_acc: %.3f G | Time: %.1f/2.0s", is_stationary, raw_acc_norm, warmup_timer);
                } else {
                    ESP_LOGI(TAG, "KINEMATICS | stat: %d | dt: %.4f | V: %5.1f %5.1f %5.1f | P: %5.1f %5.1f %5.1f", 
                             is_stationary, dt, 
                             vel_ned[0]*100.0f, vel_ned[1]*100.0f, vel_ned[2]*100.0f,
                             pos_ned[0]*100.0f, pos_ned[1]*100.0f, pos_ned[2]*100.0f);
                }
                */
            }

            
            // --- 1Hz PMIC POWER POLLING ---
            static uint32_t pmic_timer = 0;
            static int bat_pct = 0; // Static scoping ensures value survives the frame tick
            if (pmic_timer++ % 100 == 0) { // IMU Queue runs at ~100Hz
                uint8_t reg_pct = 0xA4; uint8_t pct_val = 0;
                i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, 0x34, &reg_pct, 1, &pct_val, 1, 100);
                if (pct_val <= 100) bat_pct = (int)pct_val; // Sanity bounds
            }

            // ARCHITECT FIX: Thread Decoupling. Update locked state instead of calling UI.
            taskENTER_CRITICAL(&state_spinlock);
            global_state.q = current_q;
            global_state.is_deadlocked = !sensor_data.mag_valid;
            if (pmic_timer % 100 == 1) global_state.pmic_percentage = bat_pct;
            global_state.is_moving = is_moving;
            for(int i=0; i<3; i++) {
                global_state.vel[i] = vel_ned[i];
                global_state.pos[i] = pos_ned[i];
                global_state.pure_pos[i] = pure_pos[i];
            }
            taskEXIT_CRITICAL(&state_spinlock);
        }
    }
}

void eskf_fusion_init(void) {
    ESP_LOGI(TAG, "Initializing ESKF Physics Engine...");
    ekf13 = new ekf_imu13states();
    ekf13->Init();

    calibration_init();
    kinematics_init();
    odometry_init();

    imu_queue = xQueueCreate(10, sizeof(imu_9dof_data_t));
    if (imu_queue != NULL) {
        #define ESKF_TASK_STACK_SIZE 16384
    #define ESKF_TASK_PRIORITY 5
    #define ESKF_TASK_CORE_ID 1
        xTaskCreatePinnedToCore(eskf_physics_task, "eskf_physics", ESKF_TASK_STACK_SIZE, NULL, ESKF_TASK_PRIORITY, NULL, ESKF_TASK_CORE_ID);
    }
}

void eskf_fusion_queue_data(imu_9dof_data_t *data) {
    if (imu_queue != NULL) {
        xQueueSend(imu_queue, data, 0);
    }
}

void eskf_trigger_calibration(void) {
    calibration_trigger();
}

bool eskf_is_calibrating(void) {
    return calibration_is_active();
}

void eskf_get_latest_state(eskf_state_t *out_state) {
    if (out_state) {
        taskENTER_CRITICAL(&state_spinlock);
        *out_state = global_state;
        taskEXIT_CRITICAL(&state_spinlock);
    }
}
