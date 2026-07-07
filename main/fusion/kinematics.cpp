#include "kinematics.h"
#include "physics_constants.h"
#include <math.h>

static float gx = 0, gy = 0, gz = 0;
static bool g_init = false;
static float warmup_timer = 0.0f;
static float internal_vel[3] = {0.0f, 0.0f, 0.0f};
static float internal_pos[3] = {0.0f, 0.0f, 0.0f};

void kinematics_init(void) {
    gx = 0; gy = 0; gz = 0;
    g_init = false;
    warmup_timer = 0.0f;
    for(int i=0; i<3; i++) { internal_vel[i] = 0; internal_pos[i] = 0; }
}

void kinematics_process(float dt, imu_9dof_data_t* sensor_data, quaternion_t* q, float* out_vel, float* out_pos) {
    float q0 = q->q0, q1 = q->q1, q2 = q->q2, q3 = q->q3;
    float r00 = 1.0f - 2.0f * (q2*q2 + q3*q3);
    float r01 = 2.0f * (q1*q2 - q0*q3);
    float r02 = 2.0f * (q1*q3 + q0*q2);
    float r10 = 2.0f * (q1*q2 + q0*q3);
    float r11 = 1.0f - 2.0f * (q1*q1 + q3*q3);
    float r12 = 2.0f * (q2*q3 - q0*q1);
    float r20 = 2.0f * (q1*q3 - q0*q2);
    float r21 = 2.0f * (q2*q3 + q0*q1);
    float r22 = 1.0f - 2.0f * (q1*q1 + q2*q2);

    float raw_ax = sensor_data->acc_x * IMU_ACCEL_SCALE_16G;
    float raw_ay = sensor_data->acc_y * IMU_ACCEL_SCALE_16G;
    float raw_az = sensor_data->acc_z * IMU_ACCEL_SCALE_16G;

    float raw_gx = sensor_data->gyr_x * IMU_GYRO_SCALE_2000DPS;
    float raw_gy = sensor_data->gyr_y * IMU_GYRO_SCALE_2000DPS;
    float raw_gz = sensor_data->gyr_z * IMU_GYRO_SCALE_2000DPS;

    float ax_earth = r00 * raw_ax + r01 * raw_ay + r02 * raw_az;
    float ay_earth = r10 * raw_ax + r11 * raw_ay + r12 * raw_az;
    float az_earth = r20 * raw_ax + r21 * raw_ay + r22 * raw_az;

    float raw_acc_norm = sqrtf(raw_ax*raw_ax + raw_ay*raw_ay + raw_az*raw_az);
    float raw_gyr_norm = sqrtf(raw_gx*raw_gx + raw_gy*raw_gy + raw_gz*raw_gz);

    bool is_stationary = (raw_acc_norm > 0.5f) && (fabsf(raw_acc_norm - 1.0f) < 0.05f) && (raw_gyr_norm < 3.0f);

    float a_kin_x = 0, a_kin_y = 0, a_kin_z = 0;

    if (dt > 0) {
        if (warmup_timer < 2.0f) {
            warmup_timer += dt;
            if (is_stationary) {
                gx = ax_earth; gy = ay_earth; gz = az_earth;
                g_init = true;
            }
            internal_vel[0] = 0; internal_vel[1] = 0; internal_vel[2] = 0;
            internal_pos[0] = 0; internal_pos[1] = 0; internal_pos[2] = 0;
        } else {
            if (is_stationary && g_init) {
                internal_vel[0] = 0.0f; internal_vel[1] = 0.0f; internal_vel[2] = 0.0f;

                float spring_force = dt * 2.0f;
                if (spring_force > 1.0f) spring_force = 1.0f;
                internal_pos[0] -= internal_pos[0] * spring_force;
                internal_pos[1] -= internal_pos[1] * spring_force;
                internal_pos[2] -= internal_pos[2] * spring_force;

                if (fabsf(internal_pos[0]) < 0.05f) internal_pos[0] = 0.0f;
                if (fabsf(internal_pos[1]) < 0.05f) internal_pos[1] = 0.0f;
                if (fabsf(internal_pos[2]) < 0.05f) internal_pos[2] = 0.0f;

                float alpha = dt / 0.5f; 
                if (alpha > 1.0f) alpha = 1.0f;
                gx += (ax_earth - gx) * alpha;
                gy += (ay_earth - gy) * alpha;
                gz += (az_earth - gz) * alpha;
            } else if (g_init) {
                a_kin_x = (ax_earth - gx) * GRAVITY_EARTH;
                a_kin_y = (ay_earth - gy) * GRAVITY_EARTH;
                a_kin_z = (az_earth - gz) * GRAVITY_EARTH;

                float kin_norm = sqrtf(a_kin_x*a_kin_x + a_kin_y*a_kin_y + a_kin_z*a_kin_z);
                if (kin_norm < 0.20f) {
                    a_kin_x = 0; a_kin_y = 0; a_kin_z = 0;
                }

                float leak_rate = 1.0f - (1.5f * dt); 
                if (leak_rate < 0.0f) leak_rate = 0.0f;
                
                internal_vel[0] = (internal_vel[0] * leak_rate) + (a_kin_x * dt);
                internal_vel[1] = (internal_vel[1] * leak_rate) + (a_kin_y * dt);
                internal_vel[2] = (internal_vel[2] * leak_rate) + (a_kin_z * dt);

                internal_pos[0] += internal_vel[0] * dt;
                internal_pos[1] += internal_vel[1] * dt;
                internal_pos[2] += internal_vel[2] * dt;
            }
        }
    }
    
    out_vel[0] = internal_vel[0]; out_vel[1] = internal_vel[1]; out_vel[2] = internal_vel[2];
    out_pos[0] = internal_pos[0]; out_pos[1] = internal_pos[1]; out_pos[2] = internal_pos[2];
}
