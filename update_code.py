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

    # 1. Update the Macros block in kinematics.cpp
    macros_old = """// ====================================================================
// KINEMATICS TUNING PARAMETERS
// ====================================================================
// DEADBAND: Minimum acceleration (m/s^2) required to trigger integration.
// Lower = More sensitive to gentle slides. Higher = Better drift rejection.
#define KIN_DEADBAND_MS2 0.20f 

// FRICTION: Velocity decay multiplier. Acts as "digital air resistance".
// Lower = Device glides further, better distance accuracy. 
// Higher = Device stops abruptly, prevents runaway gravity-bleed during tilts.
#define KIN_FRICTION_COEF 1.5f 
// ===================================================================="""

    macros_new = """// ====================================================================
// KINEMATICS TUNING PARAMETERS
// ====================================================================
// DEADBAND: Minimum acceleration (m/s^2) required to trigger integration.
#define KIN_DEADBAND_MS2 0.05f  // LOWERED: Allows slow, gentle slides to register

// FRICTION: Velocity decay multiplier. Acts as "digital air resistance".
#define KIN_FRICTION_COEF 0.8f  // LOWERED: Allows distance to accumulate more naturally

// WASHOUT: How fast the positional "rubber band" pulls back to 0.0 cm when resting.
// Lower = Lingers on screen longer. Higher = Snaps back instantly.
#define KIN_WASHOUT_SPEED 0.2f  // EXTRACTED: ~5 second gentle fade instead of 0.5s snap
// ===================================================================="""

    patch_file("main/fusion/kinematics.cpp", macros_old, macros_new)

    # 2. Extract the hardcoded spring force logic
    washout_old = """                float spring_force = dt * 2.0f;
                if (spring_force > 1.0f) spring_force = 1.0f;"""

    washout_new = """                float spring_force = dt * KIN_WASHOUT_SPEED;
                if (spring_force > 1.0f) spring_force = 1.0f;"""

    patch_file("main/fusion/kinematics.cpp", washout_old, washout_new)

    print("\nKinematics tuning updated. Deadband lowered and Washout parameterized.")