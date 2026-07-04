import os
import re

app_main_path = os.path.join("main", "core", "app_main.cpp")

with open(app_main_path, "r") as f:
    content = f.read()

# Locate the rotation matrix generation, whether it currently has .t() or not
pattern = re.compile(r"dspm::Mat R1 = ekf::quat2rotm\(ekf13->X\.data\)[\.t\(\)]*;")

# Inject the clean Body-to-Earth matrix
replacement = """// Get Body-to-Earth matrix. 
        // Because matrix_3d * T uses row-vector right-multiplication, 
        // the math engine inherently transposes it to Earth-to-Body for us.
        dspm::Mat R1 = ekf::quat2rotm(ekf13->X.data);"""

content, count = pattern.subn(replacement, content)

if count > 0:
    with open(app_main_path, "w") as f:
        f.write(content)
    print("Successfully removed the manual transpose to fix the double-rotation rate.")
else:
    print("Error: Could not find rotation matrix line.")