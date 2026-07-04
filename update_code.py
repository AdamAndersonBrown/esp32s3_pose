import os
import re

app_main_path = os.path.join("main", "core", "app_main.cpp")

with open(app_main_path, "r") as f:
    content = f.read()

# Locate the entire Stage 1 through Stage 3 transformation block
pattern = re.compile(
    r"BodyVectors body = stage1_hal_transform\(\(int16_t\*\)sensors_data\);.*?NedVectors ned = stage3_ned_transform\(calib\);", 
    re.DOTALL
)

# Replace it with a clean pipeline and the True Silicon Offsets
injection = """BodyVectors body = stage1_hal_transform((int16_t*)sensors_data);
        
        // ARCHITECT FIX: True Silicon Hard-Iron Offsets
        // The dies are perfectly aligned. No matrix rotation required.
        CalibratedVectors calib;
        calib.mag[0] = body.mag[0] - (-143.5f); // True X Center
        calib.mag[1] = body.mag[1] - (85.0f);   // True Y Center
        calib.mag[2] = body.mag[2] - (325.0f);  // True Z Center

        NedVectors ned = stage3_ned_transform(calib);"""

content, count = pattern.subn(injection, content)

if count > 0:
    with open(app_main_path, "w") as f:
        f.write(content)
    print("Successfully removed hardware rotation and applied True Silicon offsets.")
else:
    print("Error: Could not find the transformation block to replace.")