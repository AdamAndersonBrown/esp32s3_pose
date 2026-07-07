#include "odometry.h"

static float pure_vel[3] = {0.0f, 0.0f, 0.0f};
static float pure_pos[3] = {0.0f, 0.0f, 0.0f};
static float latched_pos[3] = {0.0f, 0.0f, 0.0f};
static bool was_moving = false;

void odometry_init(void) {
    for(int i=0; i<3; i++) {
        pure_vel[i] = 0; pure_pos[i] = 0; latched_pos[i] = 0;
    }
    was_moving = false;
}

void odometry_process(float dt, const float* a_kin, bool is_moving, bool clear_hold, float* out_distance) {
    // 1. Wipe the screen when the 5-second UI hold expires
    if (clear_hold) {
        latched_pos[0] = 0.0f;
        latched_pos[1] = 0.0f;
        latched_pos[2] = 0.0f;
    }

    if (is_moving) {
        if (!was_moving) {
            // Rising Edge: Reset accumulators for a new pure swipe
            pure_pos[0] = 0.0f; pure_pos[1] = 0.0f; pure_pos[2] = 0.0f;
            pure_vel[0] = 0.0f; pure_vel[1] = 0.0f; pure_vel[2] = 0.0f;
        }
        
        // ZERO-FRICTION Double Integration (Pure Distance)
        pure_vel[0] += a_kin[0] * dt;
        pure_vel[1] += a_kin[1] * dt;
        pure_vel[2] += a_kin[2] * dt;

        pure_pos[0] += pure_vel[0] * dt;
        pure_pos[1] += pure_vel[1] * dt;
        pure_pos[2] += pure_vel[2] * dt;

        out_distance[0] = pure_pos[0];
        out_distance[1] = pure_pos[1];
        out_distance[2] = pure_pos[2];
    } else {
        if (was_moving) {
            // Falling Edge: ZUPT Hammer! 
            // Hard-zero velocity and latch the highly accurate distance.
            pure_vel[0] = 0.0f; pure_vel[1] = 0.0f; pure_vel[2] = 0.0f;
            latched_pos[0] = pure_pos[0];
            latched_pos[1] = pure_pos[1];
            latched_pos[2] = pure_pos[2];
        }
        
        // Output the latched distance while stationary
        out_distance[0] = latched_pos[0];
        out_distance[1] = latched_pos[1];
        out_distance[2] = latched_pos[2];
    }
    
    was_moving = is_moving;
}
