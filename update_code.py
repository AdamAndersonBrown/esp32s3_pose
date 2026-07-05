import os

# Define the new HAL directory path
hal_dir = os.path.join("main", "hal")
os.makedirs(hal_dir, exist_ok=True)

hal_h_path = os.path.join(hal_dir, "hal_imu.h")
hal_cpp_path = os.path.join(hal_dir, "hal_imu.cpp")

# The strict API Contract
hal_h_content = """#pragma once

#include <stdint.h>
#include "esp_err.h"

// STRICT DATA STRUCTURE: 
// The ESKF physics engine should only ever consume this normalized struct.
// It must have zero knowledge of Bosch registers or I2C buses.
typedef struct {
    // Accelerometer (g)
    float acc_x;
    float acc_y;
    float acc_z;
    
    // Gyroscope (dps)
    float gyr_x;
    float gyr_y;
    float gyr_z;
    
    // Magnetometer (uT) - Pre-aligned and Parity-Corrected
    float mag_x;
    float mag_y;
    float mag_z;
    
    // Hardware Health Flags
    bool mag_valid; // Flags if the auxiliary bus deadlocked
} imu_9dof_data_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the BMI270 and BMM150 sensors.
 *        Handles the hardware PMIC soft-reset override and microcode upload.
 */
esp_err_t imu_hal_init(void);

/**
 * @brief Safely reads all 9-DoF data.
 *        Encapsulates the auxiliary bus gatekeeper logic to prevent FreeRTOS deadlocks.
 * @param data Pointer to the struct to populate.
 */
esp_err_t imu_hal_read_9dof(imu_9dof_data_t *data);

#ifdef __cplusplus
}
#endif
"""

# The Implementation Shell
hal_cpp_content = """#include "hal_imu.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Include your specific Bosch driver headers here
// #include "bmi270.h"
// #include "bmm150.h"

static const char *TAG = "HAL_IMU";

// Thread-safe isolation variables
static bool imu_initialized = false;

esp_err_t imu_hal_init(void) {
    ESP_LOGI(TAG, "Initializing 9-DoF Hardware Abstraction Layer...");
    
    // TODO: Migrate the 0xB6 hardware reset override here
    // TODO: Migrate the 8KB BMI270 microcode flash here
    // TODO: Migrate the BMM150 suspend-wake logic here
    
    imu_initialized = true;
    return ESP_OK;
}

esp_err_t imu_hal_read_9dof(imu_9dof_data_t *data) {
    if (!data || !imu_initialized) return ESP_ERR_INVALID_STATE;

    // TODO: Migrate the blocking I2C reads here
    // TODO: Migrate the Z-axis parity inversion and Bosch die-alignment here
    // TODO: Migrate the Hardware Variance Watchdog (caching previous frames) here
    
    return ESP_OK;
}
"""

with open(hal_h_path, "w") as f:
    f.write(hal_h_content)

with open(hal_cpp_path, "w") as f:
    f.write(hal_cpp_content)

print(f"Successfully generated HAL contract at: {hal_h_path}")
print(f"Successfully generated HAL implementation at: {hal_cpp_path}")