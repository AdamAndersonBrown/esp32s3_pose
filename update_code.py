import os
import re

def fix_ui_sleep_panic():
    filepath = "main/ui/ui_render.cpp"
    if not os.path.exists(filepath):
        print(f"ERROR: {filepath} not found.")
        return

    with open(filepath, 'r') as f:
        content = f.read()

    # 1. Remove the dangerous early return
    target_bad_return = r'if\s*\(state\.is_sleeping\)\s*return;\s*//\s*Halt\s*heavy\s*graphics\s*math\s*while\s*screen\s*is\s*off\n\s*'
    content = re.sub(target_bad_return, '', content)

    # 2. Wrap only the heavy 3D rendering in the sleep check
    # Find the start of the heavy math (usually right after the state assignment)
    math_start = "float *vel = state.vel;\n"
    math_end = "    // Update Labels"
    
    if math_start in content and math_end in content:
        parts = content.split(math_start, 1)
        inner_parts = parts[1].split(math_end, 1)
        
        safe_block = math_start + "    if (!state.is_sleeping) {\n" + inner_parts[0] + "    }\n\n" + math_end
        content = parts[0] + safe_block + inner_parts[1]
        print("Patched: ui_render.cpp (Wrapped 3D math safely, preserved LVGL object updates)")
    else:
        print("Could not find the bounds to wrap the 3D math.")

    with open(filepath, 'w') as f:
        f.write(content)

if __name__ == "__main__":
    fix_ui_sleep_panic()