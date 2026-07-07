#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void odometry_init(void);
void odometry_process(float dt, const float* a_kin, bool is_moving, bool clear_hold, float* out_distance);

#ifdef __cplusplus
}
#endif
