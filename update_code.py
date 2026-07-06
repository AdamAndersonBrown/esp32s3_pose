import os
import sys

# ARCHITECT FIX: Updated path to reflect the physical ESP-IDF directory structure
file_path = os.path.join("main", "ui", "ui_render.cpp")

if not os.path.exists(file_path):
    print(f"Error: '{file_path}' not found. Please run this script from the project root (esp32s3_pose).")
    sys.exit(1)

with open(file_path, "r") as f:
    content = f.read()

target_block = """    if (is_deadlocked) {
        lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
    }
    bsp_display_unlock();"""

replacement_block = """    if (is_deadlocked) {
        lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
    }

    // ARCHITECT FIX: Inject Kinematics into LVGL Labels (Integer Cast for OS Safety)
    lv_label_set_text_fmt(vel_label, "V(cm/s): %5d %5d %5d", 
                          (int)(vel[0] * 100.0f), (int)(vel[1] * 100.0f), (int)(vel[2] * 100.0f));
    lv_label_set_text_fmt(pos_label, "P(cm): %5d %5d %5d", 
                          (int)(pos[0] * 100.0f), (int)(pos[1] * 100.0f), (int)(pos[2] * 100.0f));

    bsp_display_unlock();"""

if target_block in content:
    new_content = content.replace(target_block, replacement_block)
    with open(file_path, "w") as f:
        f.write(new_content)
    print(f"SUCCESS: Architectural patch applied to {file_path}. UI text formatters injected.")
else:
    print(f"FAILURE: Target string not found in {file_path}. Aborting to prevent regression.")
    sys.exit(1)