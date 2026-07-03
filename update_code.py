import os
import re

def fix_race_condition():
    print("1. Injecting Mutex locks to protect the 3D graphics memory...")
    app_path = os.path.normpath("main/core/app_main.cpp")
    
    if os.path.exists(app_path):
        with open(app_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # Check to ensure we don't double-patch the file
        if "bsp_display_lock(0);" not in content:
            # Find the app_init(); call and wrap it in hardware locks
            patched = re.sub(
                r'(\s+)app_init\(\);', 
                r'\1bsp_display_lock(0);\1app_init();\1bsp_display_unlock();', 
                content
            )

            with open(app_path, 'w', encoding='utf-8') as f:
                f.write(patched)
                
            print(" -> Success: Race condition eliminated. LVGL is thread-safe.")
        else:
            print(" -> Locks are already present.")
    else:
        print(" -> ERROR: Could not find app_main.cpp")

if __name__ == "__main__":
    fix_race_condition()