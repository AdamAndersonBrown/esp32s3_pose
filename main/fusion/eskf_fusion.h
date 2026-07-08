#pragma once
#include "hal_imu.h"

// STRICT DATA STRUCTURE:
// The unified output of the physics engine.
typedef struct {
    float q0, q1, q2, q3;
} quaternion_t;

// ARCHITECT FIX: Thread-safe state container for UI polling
typedef struct {
    quaternion_t q;
    float vel[3];
    float pos[3];
    float pure_pos[3];
    bool is_deadlocked;
    bool is_moving;
    int pmic_percentage;
    bool is_sleeping;
} eskf_state_t;

extern eskf_state_t global_state;

#ifdef __cplusplus
extern "C" {
#endif

void eskf_get_latest_state(eskf_state_t *out_state);

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
