#pragma once
#include "eskf_fusion.h"
#include "hal_imu.h"

#ifdef __cplusplus
extern "C" {
#endif

void kinematics_init(void);
void kinematics_process(float dt, imu_9dof_data_t* sensor_data, quaternion_t* q, float* out_vel, float* out_pos);

#ifdef __cplusplus
}
#endif
