import os
import re

def fix_fork_bomb_permanently():
    filepath = "main/ui/ui_render.cpp"
    if not os.path.exists(filepath):
        print(f"ERROR: {filepath} not found.")
        return

    with open(filepath, 'r') as f:
        content = f.read()

    # 1. Annihilate EVERY lv_timer_create in the file to clear the fork bomb
    content = re.sub(r'\s*lv_timer_create\s*\(.*?\)\s*;', '', content)

    # 2. Isolate ui_render_init and inject the timer safely inside its lock
    # Split the file exactly at the initialization function signature
    parts = content.split('void ui_render_init(void)', 1)
    if len(parts) == 2:
        before_init = parts[0]
        after_init_decl = parts[1]
        
        # Find the very first unlock *after* the initialization declaration
        init_parts = after_init_decl.split('bsp_display_unlock();', 1)
        if len(init_parts) == 2:
            safe_timer_inject = "\n    lv_timer_create(ui_render_timer_cb, 50, NULL);\n    bsp_display_unlock();"
            content = before_init + 'void ui_render_init(void)' + init_parts[0] + safe_timer_inject + init_parts[1]
            print(f"Patched: {filepath} (Timer safely anchored exclusively inside ui_render_init)")
        else:
            print("Could not find unlock inside ui_render_init")
    else:
        print("Could not find ui_render_init")

    with open(filepath, 'w') as f:
        f.write(content)

if __name__ == "__main__":
    fix_fork_bomb_permanently()