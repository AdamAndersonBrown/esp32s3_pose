import os
import re

def fix_pmic_fuel_gauge():
    # 1. Update the state structure header
    hdr = "main/fusion/eskf_fusion.h"
    if os.path.exists(hdr):
        with open(hdr, 'r') as f: content = f.read()
        content = content.replace('float pmic_current_ma;', 'int pmic_percentage;')
        with open(hdr, 'w') as f: f.write(content)

    # 2. Update Physics Task polling logic
    eskf = "main/fusion/eskf_fusion.cpp"
    if os.path.exists(eskf):
        with open(eskf, 'r') as f: content = f.read()
        
        old_poll = r'// --- 1Hz PMIC POWER POLLING ---.*?// ARCHITECT FIX: Thread Decoupling\.'
        new_poll = """// --- 1Hz PMIC POWER POLLING ---
            static uint32_t pmic_timer = 0;
            static int bat_pct = 0; // Static scoping ensures value survives the frame tick
            if (pmic_timer++ % 100 == 0) { // IMU Queue runs at ~100Hz
                uint8_t reg_pct = 0xA4; uint8_t pct_val = 0;
                i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, 0x34, &reg_pct, 1, &pct_val, 1, 100);
                if (pct_val <= 100) bat_pct = (int)pct_val; // Sanity bounds
            }

            // ARCHITECT FIX: Thread Decoupling."""
        content = re.sub(old_poll, new_poll, content, flags=re.DOTALL)
        
        content = content.replace('global_state.pmic_current_ma = net_current;', 'global_state.pmic_percentage = bat_pct;')
        with open(eskf, 'w') as f: f.write(content)

    # 3. Update the UI string formatting
    ui = "main/ui/ui_render.cpp"
    if os.path.exists(ui):
        with open(ui, 'r') as f: content = f.read()
        
        old_ui = r'if\s*\(state\.pmic_current_ma\s*>\s*0\)\s*\{.*?\}\s*else\s*\{.*?\}'
        new_ui = 'lv_label_set_text_fmt(pmic_label, "BAT: %d%%", state.pmic_percentage);'
        
        content = re.sub(old_ui, new_ui, content, flags=re.DOTALL)
        with open(ui, 'w') as f: f.write(content)

    print("Patched: PMIC now natively leverages the AXP2101 E-Gauge Register (0xA4)")

if __name__ == "__main__":
    fix_pmic_fuel_gauge()