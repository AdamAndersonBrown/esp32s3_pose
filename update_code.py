import os
import re

app_main_path = os.path.join("main", "core", "app_main.cpp")

with open(app_main_path, "r") as f:
    content = f.read()

# Target the overly-aggressive Accel covariance we set earlier
pattern = re.compile(r"float R_m\[6\]\s*=\s*\{\s*0\.001f,\s*0\.001f,\s*0\.001f,\s*0\.03f,\s*0\.03f,\s*0\.03f\s*\};")

replacement = r"""// ARCHITECT FIX: Kinetic Acceleration Tolerance
        // Raised Accel covariance to 0.5 to reject centripetal hand movement.
        // Gyro now handles dynamic 3D tilt; Accel handles long-term gravity leveling.
        float R_m[6] = {0.5f, 0.5f, 0.5f, 0.03f, 0.03f, 0.03f};"""

content, count = pattern.subn(replacement, content)

if count > 0:
    with open(app_main_path, "w") as f:
        f.write(content)
    print("Successfully tuned ESKF to reject kinetic acceleration.")
else:
    print("Error: Could not locate the R_m covariance array.")