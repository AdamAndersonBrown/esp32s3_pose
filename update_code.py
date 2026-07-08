import os
import re

def fix_lvgl_boot_race():
    filepath = "main/ui/ui_render.cpp"
    if not os.path.exists(filepath):
        print(f"ERROR: {filepath} not found.")
        return

    with open(filepath, 'r') as f:
        content = f.read()

    # 1. Excise the illegally placed styling command that occurs before the lock
    bad_style = r'[ \t]*// ARCHITECT FIX: Set initial screen background to dark grey\n[ \t]*lv_obj_set_style_bg_color\(lv_screen_active\(\), lv_color_hex\(0x222222\), 0\);\n'
    content = re.sub(bad_style, '', content)

    # 2. Fix the overflowed lock and inject the styling command securely inside the boundary
    bad_lock = r'lvgl_port_lock\(0xFFFFFFFF\);'
    safe_lock = (
        "while(!lvgl_port_lock(100)) { vTaskDelay(pdMS_TO_TICKS(10)); }\n\n"
        "    // ARCHITECT FIX: Safely apply background color inside the secured mutex\n"
        "    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x222222), 0);"
    )

    if re.search(bad_lock, content):
        content = re.sub(bad_lock, safe_lock, content)
        with open(filepath, 'w') as f:
            f.write(content)
        print("Patched: ui_render.cpp (Relocated LVGL styling inside safe mutex boundary)")
    else:
        print("Error: Could not find lvgl_port_lock(0xFFFFFFFF);")

if __name__ == '__main__':
    fix_lvgl_boot_race()