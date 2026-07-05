#pragma once
#include "hal_imu.h"

// STRICT DATA STRUCTURE:
// The unified output of the physics engine.
typedef struct {
    float q0;
    float q1;
    float q2;
    float q3;
} quaternion_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the ESKF Physics Engine.
 *        Loads hard-iron offsets and measurement noise covariance matrices.
 */
void eskf_fusion_init(void);

/**
 * @brief Fuses 9-DoF telemetry into an absolute 3D orientation.
 * @param sensor_data The raw telemetry from the HAL.
 * @return The updated orientation quaternion.
 */
quaternion_t eskf_fusion_update(imu_9dof_data_t *sensor_data);

#ifdef __cplusplus
}
#endif
