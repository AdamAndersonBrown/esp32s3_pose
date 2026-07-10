import os

def main():
    print("Initiating Senior Architect Patch: Load Shedding and Floodgate Alignment...\n")

    pm_path = os.path.join('main', 'core', 'power_manager.cpp')
    if os.path.exists(pm_path):
        with open(pm_path, 'r') as f:
            content = f.read()

        # Patch 1: LDO Masking
        old_ldo = "        uint8_t optimized_val = aldo_val | 0x0F; "
        new_ldo = """        // ARCHITECT FIX: Hardware Severance. 
        // 0x0A (0000 1010) keeps ALDO2 (LCD) and ALDO4 (Touch/AW9523B) ON.
        // It actively KILLS ALDO 1 and 3 (Audio Amp, Camera).
        uint8_t optimized_val = (aldo_val & 0xF0) | 0x0A; """
        if old_ldo in content:
            content = content.replace(old_ldo, new_ldo)
            print("[SUCCESS] Patched ALDO Masking (Hardware Severance Applied)")

        # Patch 2: VBUS Limit in open_charging_floodgates
        old_vbus = "        val_vbus = (val_vbus & 0xF8) | 0x01; // ARCHITECT FIX: 0x01 = 500mA limit to prevent PC polyfuse trip"
        new_vbus = "        val_vbus = (val_vbus & 0xF8) | 0x04; // ARCHITECT FIX: Unified to 1.5A (0x04)"
        if old_vbus in content:
            content = content.replace(old_vbus, new_vbus)
            print("[SUCCESS] Patched open_charging_floodgates (1.5A VBUS Target Set)")

        # Patch 3: 10Hz Fast Loop Reg 62 Alignment
        old_62 = """                if (val62 != 0x13) { // Force 1.0A
                    uint8_t write62[2] = {0x62, 0x13};"""
        new_62 = """                if (val62 != 0x1B) { // ARCHITECT FIX: Align to 1.0A (0x1B) instead of 0x13
                    uint8_t write62[2] = {0x62, 0x1B};"""
        if old_62 in content:
            content = content.replace(old_62, new_62)
            print("[SUCCESS] Patched Fast Loop (1.0A Charge Current Aligned)")

        # Patch 4: Take Down the Fence (Initial Boot)
        old_setup = "    // open_charging_floodgates(); // ARCHITECT FIX: Chesterton's Fence. Let the AXP2101 manage itself."
        new_setup = "    open_charging_floodgates(); // ARCHITECT FIX: UNCOMMENTED! Take down the fence."
        if old_setup in content:
            content = content.replace(old_setup, new_setup)
            print("[SUCCESS] Patched Daemon Setup (Floodgates Unleashed)")

        # Patch 5: Take Down the Fence (5-Second Audit Loop)
        old_audit = """            // VBUS Limit Check (1.5A) OR Charge Current Check (1.0A = 0x1B)
            if (((vals[2] & 0x07) != 0x01) || (vals[4] != 0x1B)) { // ARCHITECT FIX: Audit for 500mA
                ESP_LOGW(TAG, "Hardware override dropped! Re-opening charging floodgates...");
                // open_charging_floodgates(); // ARCHITECT FIX: Chesterton's Fence. Let the AXP2101 manage itself.
            }"""
        new_audit = """            // VBUS Limit Check (1.5A = 0x04) OR Charge Current Check (1.0A = 0x1B)
            if (((vals[2] & 0x07) != 0x04) || (vals[4] != 0x1B)) { // ARCHITECT FIX: Audit for 1.5A
                ESP_LOGW(TAG, "Hardware override dropped! Re-opening charging floodgates...");
                open_charging_floodgates(); // ARCHITECT FIX: UNCOMMENTED! Defend against VHOLD cable sag.
            }"""
        if old_audit in content:
            content = content.replace(old_audit, new_audit)
            print("[SUCCESS] Patched 5-Second Audit (VHOLD Cable Sag Defense Active)")

        with open(pm_path, 'w') as f:
            f.write(content)
        print("\nPatch complete. All topologies aligned. Please build and flash.")
    else:
        print(f"[FATAL] File not found: {pm_path}")

if __name__ == "__main__":
    main()