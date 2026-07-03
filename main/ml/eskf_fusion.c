#include "eskf_fusion.h"
#include <math.h>

nominal_state_t state;
float P_cov[15 * 15];
float F_jac[15 * 15]; 

void eskf_init(void) {
    state.q[0] = 1.0f; state.q[1] = 0.0f; state.q[2] = 0.0f; state.q[3] = 0.0f;
    for(int i=0; i<3; i++) { state.gyro_b[i] = 0; state.accel_b[i] = 0; }
    for(int i=0; i<225; i++) P_cov[i] = 0.0f;
    for(int i=0; i<15; i++) P_cov[i*15 + i] = 0.01f; 
}

void eskf_predict(float gx, float gy, float gz, float ax, float ay, float az, float dt) {
    float wx = gx - state.gyro_b[0];
    float wy = gy - state.gyro_b[1];
    float wz = gz - state.gyro_b[2];
    
    // --- ESKF INJECTION PHASE (Accelerometer Gravity Vector Correction) ---
    // This clamps the gyroscope's random-walk drift in pitch and roll
    float norm_a = sqrtf(ax*ax + ay*ay + az*az);
    if (norm_a > 0.1f) {
        ax /= norm_a; ay /= norm_a; az /= norm_a;
        
        // Estimated gravity direction extracted from the current quaternion
        float vx = 2.0f * (state.q[1]*state.q[3] - state.q[0]*state.q[2]);
        float vy = 2.0f * (state.q[0]*state.q[1] + state.q[2]*state.q[3]);
        float vz = state.q[0]*state.q[0] - state.q[1]*state.q[1] - state.q[2]*state.q[2] + state.q[3]*state.q[3];
        
        // Cross product error between estimated and measured gravity
        float ex = (ay * vz - az * vy);
        float ey = (az * vx - ax * vz);
        float ez = (ax * vy - ay * vx);

        // Inject error feedback into gyro rates (Proportional Gain)
        float Kp = 2.5f; 
        wx += Kp * ex;
        wy += Kp * ey;
        wz += Kp * ez;
    }
    // ----------------------------------------------------------------------

    float norm_w = sqrtf(wx*wx + wy*wy + wz*wz);
    if (norm_w > 1e-5f) {
        float theta = norm_w * dt;
        float half_theta = theta * 0.5f;
        float sin_half = sinf(half_theta);
        
        float dq[4] = {
            cosf(half_theta),
            (wx/norm_w) * sin_half,
            (wy/norm_w) * sin_half,
            (wz/norm_w) * sin_half
        };

        float q_new[4];
        q_new[0] = state.q[0]*dq[0] - state.q[1]*dq[1] - state.q[2]*dq[2] - state.q[3]*dq[3];
        q_new[1] = state.q[0]*dq[1] + state.q[1]*dq[0] + state.q[2]*dq[3] - state.q[3]*dq[2];
        q_new[2] = state.q[0]*dq[2] - state.q[1]*dq[3] + state.q[2]*dq[0] + state.q[3]*dq[1];
        q_new[3] = state.q[0]*dq[3] + state.q[1]*dq[2] - state.q[2]*dq[1] + state.q[3]*dq[0];
        
        float q_norm = sqrtf(q_new[0]*q_new[0] + q_new[1]*q_new[1] + q_new[2]*q_new[2] + q_new[3]*q_new[3]);
        for(int i=0; i<4; i++) state.q[i] = q_new[i] / q_norm;
    }

    float temp_P[225];
    dspm_mult_f32(F_jac, P_cov, temp_P, 15, 15, 15); 
}

void eskf_get_quaternion(float *q_out) {
    q_out[0] = state.q[0]; q_out[1] = state.q[1];
    q_out[2] = state.q[2]; q_out[3] = state.q[3];
}
