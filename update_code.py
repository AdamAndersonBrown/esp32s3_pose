import os
import re

cmake_path = os.path.join("main", "CMakeLists.txt")

with open(cmake_path, "r") as f:
    content = f.read()

# 1. Strip out the botched file and directory injections
bad_strings = [
    '"hal/hal_imu.cpp"', '"fusion/eskf_fusion.cpp"', '"ui/ui_render.cpp"',
    '"hal"', '"fusion"', '"ui"'
]
for string in bad_strings:
    content = content.replace(string, '')

# Clean up any lingering blank lines left by the removal
content = re.sub(r'\n\s*\n', '\n', content)

# 2. Safely inject the CPP files directly adjacent to SRCS
if "SRCS" in content:
    content = content.replace("SRCS", 'SRCS\n                    "hal/hal_imu.cpp"\n                    "fusion/eskf_fusion.cpp"\n                    "ui/ui_render.cpp"')

# 3. Safely inject the folders directly adjacent to INCLUDE_DIRS
if "INCLUDE_DIRS" in content:
    content = content.replace("INCLUDE_DIRS", 'INCLUDE_DIRS\n                    "hal"\n                    "fusion"\n                    "ui"')
else:
    # Failsafe if INCLUDE_DIRS was missing
    content = content.replace(")", '\n                    INCLUDE_DIRS "hal" "fusion" "ui"\n)')

with open(cmake_path, "w") as f:
    f.write(content)

print("Successfully repaired CMakeLists.txt!")