import os
import re

def fix_fork_bomb(filepath):
    if not os.path.exists(filepath):
        print(f"ERROR: {filepath} not found.")
        return
        
    with open(filepath, 'r') as f:
        content = f.read()

    # The regex in Phase 5 injected the timer creation twice. 
    # We want to remove the one at the very end of the file (inside the callback).
    # We will use rsplit to find the LAST occurrence and revert it.
    
    bad_block = """    bsp_display_unlock();
    
    // Ping physics state at exactly 20Hz (50ms)
    lv_timer_create(ui_render_timer_cb, 50, NULL);
}"""

    good_block = """    bsp_display_unlock();
}"""

    # Split from the right, replacing only the final occurrence
    parts = content.rsplit(bad_block, 1)
    
    if len(parts) == 2:
        content = parts[0] + good_block + parts[1]
        with open(filepath, 'w') as f:
            f.write(content)
        print(f"Patched: {filepath} (Timer fork bomb neutralized)")
    else:
        # Fallback for slight whitespace variations
        content = re.sub(r'bsp_display_unlock\(\);\s*// Ping physics state at exactly 20Hz \(50ms\)\s*lv_timer_create\(ui_render_timer_cb, 50, NULL\);\s*\}\s*$', '    bsp_display_unlock();\n}\n', content)
        with open(filepath, 'w') as f:
            f.write(content)
        print(f"Patched: {filepath} (Timer fork bomb neutralized via regex)")

if __name__ == "__main__":
    fix_fork_bomb("main/ui/ui_render.cpp")