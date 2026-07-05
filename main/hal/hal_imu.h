#pragma once

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
