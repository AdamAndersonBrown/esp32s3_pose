import os

fusion_cpp = os.path.join("main", "fusion", "eskf_fusion.cpp")
ui_cpp = os.path.join("main", "ui", "ui_render.cpp")

# 1. Inject missing transitive dependencies into the Physics Layer
with open(fusion_cpp, "r") as f:
    content = f.read()

headers_to_inject = """#include <math.h>
#include "sensor_hal.h"
#include "cube_matrix.h"
#include "image_to_3d_matrix.h"

// Failsafe definitions in case the BSP math headers are aggressively pruned
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef DEG_TO_RAD
#define DEG_TO_RAD (M_PI / 180.0f)
#endif
"""

if "#include <math.h>" not in content:
    content = content.replace('#include "esp_dsp.h"', headers_to_inject + '\n#include "esp_dsp.h"')

with open(fusion_cpp, "w") as f:
    f.write(content)

# 2. Clean up the unused TAG warning in the UI Layer
with open(ui_cpp, "r") as f:
    ui_content = f.read()

ui_content = ui_content.replace('static const char *TAG = "UI_RENDER";', '// static const char *TAG = "UI_RENDER";')

with open(ui_cpp, "w") as f:
    f.write(ui_content)

print("Successfully resolved transitive dependencies and math macros!")