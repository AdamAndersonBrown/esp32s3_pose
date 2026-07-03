#ifndef ESKF_FUSION_H
#define ESKF_FUSION_H

#include <stdint.h>
#include "esp_dsp.h"

typedef struct {
    float q[4];
    float gyro_b[3];
    float accel_b[3];
} nominal_state_t;

extern float P_cov[15 * 15]; 

void eskf_init(void);
void eskf_predict(float gx, float gy, float gz, float ax, float ay, float az, float dt);
void eskf_update_mag(float mx, float my, float mz);
void eskf_get_quaternion(float *q_out);

#endif
