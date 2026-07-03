import os
import re

def patch_bsp():
    print("1. Neutralizing BSP Touch Panic...")
    # Target the downloaded M5Stack vendor library
    bsp_path = os.path.normpath("managed_components/espressif__m5stack_core_s3/m5stack_core_s3.c")

    if os.path.exists(bsp_path):
        with open(bsp_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # Find the aggressive error check and bypass it so the board ignores the touch failure
        patched_content = re.sub(
            r'ESP_ERROR_CHECK\(\s*bsp_touch_new\(\s*NULL\s*,\s*&tp\s*\)\s*\);',
            r'/* ESP_ERROR_CHECK bypassed for IDF v5.3 */ bsp_touch_new(NULL, &tp);',
            content
        )

        with open(bsp_path, 'w', encoding='utf-8') as f:
            f.write(patched_content)
            
        print(" -> Success: Touch panic safely bypassed.")
    else:
        print(" -> ERROR: M5Stack BSP file not found.")

if __name__ == "__main__":
    patch_bsp()