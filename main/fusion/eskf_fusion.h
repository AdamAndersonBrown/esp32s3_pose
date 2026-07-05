#pragma once
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

/**
 * @brief Triggers the dynamic 3D Figure-8 Hard-Iron calibration sequence.
 */
void eskf_trigger_calibration(void);

#ifdef __cplusplus
bool eskf_is_calibrating(void);
}
#endif
