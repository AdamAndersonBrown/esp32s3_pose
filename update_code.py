import os
import sys

file_path = os.path.join("main", "fusion", "eskf_fusion.cpp")

if not os.path.exists(file_path):
    print(f"Error: '{file_path}' not found. Please run this script from the project root.")
    sys.exit(1)

with open(file_path, "r") as f:
    content = f.read()

target_block = """                // 1. Accelerometer Inflation (Centripetal & Kinetic Noise Rejection)
                // Earth gravity is exactly 1.0G. Any deviation indicates kinetic movement.
                float current_acc_norm = accel_input_mat.norm();
                float acc_distortion = fabsf(current_acc_norm - 1.0f);
                if (acc_distortion > 0.05f) {
                    // Exponentially detach the virtual horizon during cornering/acceleration
                    acc_var += (acc_distortion * 20.0f); 
                }

                // 2. Magnetometer Inflation (EMI & Hard-Iron Rejection)
                if (spherical_distortion > 0.05f) {
                    mag_var += (spherical_distortion * 15.0f); 
                }"""

replacement_block = """                // 1. Accelerometer Inflation (Centripetal & Kinetic Noise Rejection)
                // Earth gravity is exactly 1.0G. Any deviation indicates kinetic movement.
                float current_acc_norm = accel_input_mat.norm();
                float acc_distortion = fabsf(current_acc_norm - 1.0f);
                if (acc_distortion > 0.05f) {
                    // ARCHITECT FIX: Squared exponential penalty (n=2) for off-road impacts
                    acc_var += (acc_distortion * acc_distortion * 20.0f); 
                }

                // 2. Magnetometer Inflation (EMI & Hard-Iron Rejection)
                if (spherical_distortion > 0.05f) {
                    // ARCHITECT FIX: Squared exponential penalty (n=2) to reject severe powertrain EMI
                    mag_var += (spherical_distortion * spherical_distortion * 15.0f); 
                }"""

if target_block in content:
    new_content = content.replace(target_block, replacement_block)
    with open(file_path, "w") as f:
        f.write(new_content)
    print(f"SUCCESS: Architectural patch applied to {file_path}. Adaptive ESKF upgraded to squared exponential penalties.")
else:
    print(f"FAILURE: Target string not found in {file_path}. Aborting to prevent regression.")
    sys.exit(1)