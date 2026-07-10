import os
import re

def main():
    print("Initiating Senior Architect Patch: Restoring Standard UI Telemetry...\n")
    
    # --- RESTORE POWER MANAGER (READ REAL TEMPERATURE) ---
    pm_path = os.path.join('main', 'core', 'power_manager.cpp')
    if os.path.exists(pm_path):
        with open(pm_path, 'r') as f:
            pm_content = f.read()

        # Target the X-Ray hijack block
        replacement_pm = '''if (temp_handle != NULL) {
                        float tsens_value = 0.0f;
                        temperature_sensor_get_celsius(temp_handle, &tsens_value);
                        global_state.system_temp = tsens_value;
                    }'''
        
        pm_content = re.sub(
            r'// X-Ray: Pack Reg 0x00, Reg 0x01, and E-Gauge\s*uint32_t packed = [^;]+;\s*global_state\.system_temp = \(float\)packed;',
            replacement_pm,
            pm_content,
            flags=re.DOTALL
        )
        
        with open(pm_path, 'w') as f:
            f.write(pm_content)
        print(f"[SUCCESS] Patched {pm_path} (Real Temperature Sensor Restored)")
    else:
        print(f"[FATAL] {pm_path} not found.")

    # --- RESTORE UI RENDER (FORMAT AS FLOAT) ---
    ui_path = os.path.join('main', 'ui', 'ui_render.cpp')
    if os.path.exists(ui_path):
        with open(ui_path, 'r') as f:
            ui_content = f.read()

        # Target the hex unpacking block
        replacement_ui = '''int temp_whole = (int)global_state.system_temp;
        int temp_frac = (int)((global_state.system_temp - temp_whole) * 10.0f);
        if (temp_frac < 0) temp_frac = -temp_frac; 
        lv_label_set_text_fmt(temp_label, "SYS: %d.%d C", temp_whole, temp_frac);'''

        ui_content = re.sub(
            r'uint32_t packed = \(uint32_t\)global_state\.system_temp;.*?lv_label_set_text_fmt\(temp_label,.*?packed & 0xFF\)\);',
            replacement_ui,
            ui_content,
            flags=re.DOTALL
        )
        
        with open(ui_path, 'w') as f:
            f.write(ui_content)
        print(f"[SUCCESS] Patched {ui_path} (Temperature Formatting Restored)")
    else:
        print(f"[FATAL] {ui_path} not found.")

    print("\nPatch complete. Run 'idf.py build flash monitor' to deploy the final release build.")

if __name__ == "__main__":
    main()