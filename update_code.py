import os

def patch_file(filepath, old_text, new_text):
    if not os.path.exists(filepath):
        print(f"ERROR: {filepath} not found.")
        return
    with open(filepath, 'r') as f:
        content = f.read()
    
    if old_text not in content:
        print(f"WARNING: Target block not found in {filepath}. Skipping.")
        return
        
    content = content.replace(old_text, new_text)
    with open(filepath, 'w') as f:
        f.write(content)
    print(f"Patched: {filepath}")

if __name__ == "__main__":

    # ---------------------------------------------------------
    # 1. KINEMATICS ENGINE (Hold Timer & State Output)
    # ---------------------------------------------------------
    k_h_old = "void kinematics_process(float dt, imu_9dof_data_t* sensor_data, quaternion_t* q, float* out_vel, float* out_pos);"
    k_h_new = "void kinematics_process(float dt, imu_9dof_data_t* sensor_data, quaternion_t* q, float* out_vel, float* out_pos, bool* out_moving);"
    patch_file("main/fusion/kinematics.h", k_h_old, k_h_new)

    k_cpp_macro_old = """// WASHOUT: How fast the positional "rubber band" pulls back to 0.0 cm when resting.
// Lower = Lingers on screen longer. Higher = Snaps back instantly.
#define KIN_WASHOUT_SPEED 0.2f  // EXTRACTED: ~5 second gentle fade instead of 0.5s snap"""
    k_cpp_macro_new = """// HOLD TIME: How many seconds to freeze the position on screen before resetting to 0.0 cm.
#define KIN_HOLD_TIME_S 5.0f"""
    patch_file("main/fusion/kinematics.cpp", k_cpp_macro_old, k_cpp_macro_new)

    k_cpp_sig_old = "void kinematics_process(float dt, imu_9dof_data_t* sensor_data, quaternion_t* q, float* out_vel, float* out_pos) {"
    k_cpp_sig_new = """void kinematics_process(float dt, imu_9dof_data_t* sensor_data, quaternion_t* q, float* out_vel, float* out_pos, bool* out_moving) {
    static float stationary_timer = 0.0f;"""
    patch_file("main/fusion/kinematics.cpp", k_cpp_sig_old, k_cpp_sig_new)

    k_cpp_wash_old = """                internal_vel[0] = 0.0f; internal_vel[1] = 0.0f; internal_vel[2] = 0.0f;

                float spring_force = dt * KIN_WASHOUT_SPEED;
                if (spring_force > 1.0f) spring_force = 1.0f;
                internal_pos[0] -= internal_pos[0] * spring_force;
                internal_pos[1] -= internal_pos[1] * spring_force;
                internal_pos[2] -= internal_pos[2] * spring_force;

                if (fabsf(internal_pos[0]) < 0.05f) internal_pos[0] = 0.0f;
                if (fabsf(internal_pos[1]) < 0.05f) internal_pos[1] = 0.0f;
                if (fabsf(internal_pos[2]) < 0.05f) internal_pos[2] = 0.0f;

                float alpha = dt / 0.5f;"""
    k_cpp_wash_new = """                internal_vel[0] = 0.0f; internal_vel[1] = 0.0f; internal_vel[2] = 0.0f;

                stationary_timer += dt;
                if (stationary_timer >= KIN_HOLD_TIME_S) {
                    internal_pos[0] = 0.0f;
                    internal_pos[1] = 0.0f;
                    internal_pos[2] = 0.0f;
                }
                *out_moving = false;

                float alpha = dt / 0.5f;"""
    patch_file("main/fusion/kinematics.cpp", k_cpp_wash_old, k_cpp_wash_new)

    k_cpp_mov_old = """            } else if (g_init) {
                a_kin_x = (ax_earth - gx) * GRAVITY_EARTH;"""
    k_cpp_mov_new = """            } else if (g_init) {
                stationary_timer = 0.0f;
                *out_moving = true;
                
                a_kin_x = (ax_earth - gx) * GRAVITY_EARTH;"""
    patch_file("main/fusion/kinematics.cpp", k_cpp_mov_old, k_cpp_mov_new)

    # ---------------------------------------------------------
    # 2. ESKF STATE PASSING
    # ---------------------------------------------------------
    e_h_old = """    float pos[3];
    bool is_deadlocked;
} eskf_state_t;"""
    e_h_new = """    float pos[3];
    bool is_deadlocked;
    bool is_moving;
} eskf_state_t;"""
    patch_file("main/fusion/eskf_fusion.h", e_h_old, e_h_new)

    e_cpp_call_old = """            static float vel_ned[3] = {0.0f, 0.0f, 0.0f};
            static float pos_ned[3] = {0.0f, 0.0f, 0.0f};
            
            kinematics_process(dt, &sensor_data, &current_q, vel_ned, pos_ned);"""
    e_cpp_call_new = """            static float vel_ned[3] = {0.0f, 0.0f, 0.0f};
            static float pos_ned[3] = {0.0f, 0.0f, 0.0f};
            bool is_moving = false;
            
            kinematics_process(dt, &sensor_data, &current_q, vel_ned, pos_ned, &is_moving);"""
    patch_file("main/fusion/eskf_fusion.cpp", e_cpp_call_old, e_cpp_call_new)

    e_cpp_state_old = """            global_state.is_deadlocked = !sensor_data.mag_valid;"""
    e_cpp_state_new = """            global_state.is_deadlocked = !sensor_data.mag_valid;
            global_state.is_moving = is_moving;"""
    patch_file("main/fusion/eskf_fusion.cpp", e_cpp_state_old, e_cpp_state_new)

    # ---------------------------------------------------------
    # 3. UI RENDERER (Background Toggles)
    # ---------------------------------------------------------
    ui_init_old = """    display = bsp_display_start();
    init_perspective_matrix(perspective_matrix);"""
    ui_init_new = """    display = bsp_display_start();
    init_perspective_matrix(perspective_matrix);
    
    // ARCHITECT FIX: Set initial screen background to dark grey
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x222222), 0);"""
    patch_file("main/ui/ui_render.cpp", ui_init_old, ui_init_new)

    ui_cb_old = """    quaternion_t *q = &state.q;
    bool is_deadlocked = state.is_deadlocked;
    float *vel = state.vel;
    float *pos = state.pos;"""
    ui_cb_new = """    quaternion_t *q = &state.q;
    bool is_deadlocked = state.is_deadlocked;
    float *vel = state.vel;
    float *pos = state.pos;
    bool is_moving = state.is_moving;

    static bool was_moving = false;
    if (is_moving != was_moving) {
        if (is_moving) {
            lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x660000), 0); // Dark Red
        } else {
            lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x222222), 0); // Dark Grey
        }
        was_moving = is_moving;
    }"""
    patch_file("main/ui/ui_render.cpp", ui_cb_old, ui_cb_new)

    print("\nKinematics hold timer and dynamic UI recording indicators injected.")