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
    # 1. INJECT EMPIRICAL ZUPT MACROS
    # ---------------------------------------------------------
    macros_old = """// HOLD TIME: How many seconds to freeze the position on screen before resetting to 0.0 cm.
#define KIN_HOLD_TIME_S 5.0f
// ===================================================================="""

    macros_new = """// HOLD TIME: How many seconds to freeze the position on screen before resetting to 0.0 cm.
#define KIN_HOLD_TIME_S 5.0f

// ZUPT THRESHOLDS: Derived from 12-hour empirical static capture.
// Accel Max Dev: ~7 LSB (0.0034 G) | Gyro Max Dev: ~2 LSB (0.122 DPS)
#define ZUPT_ACCEL_TOLERANCE_G 0.01f
#define ZUPT_GYRO_TOLERANCE_DPS 0.5f
// ===================================================================="""

    patch_file("main/fusion/kinematics.cpp", macros_old, macros_new)

    # ---------------------------------------------------------
    # 2. APPLY THRESHOLDS TO THE STATIONARY GATE
    # ---------------------------------------------------------
    eval_old = """    bool is_stationary = (raw_acc_norm > 0.5f) && (fabsf(raw_acc_norm - 1.0f) < 0.05f) && (raw_gyr_norm < 3.0f);"""
    
    eval_new = """    bool is_stationary = (raw_acc_norm > 0.5f) && (fabsf(raw_acc_norm - 1.0f) < ZUPT_ACCEL_TOLERANCE_G) && (raw_gyr_norm < ZUPT_GYRO_TOLERANCE_DPS);"""

    patch_file("main/fusion/kinematics.cpp", eval_old, eval_new)

    print("\nEmpirical ZUPT boundaries successfully applied.")