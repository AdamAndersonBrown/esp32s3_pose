import os
import re

def lock_vbus_telemetry():
    pm_path = "main/core/power_manager.cpp"
    if not os.path.exists(pm_path):
        print(f"ERROR: {pm_path} not found.")
        return

    with open(pm_path, 'r') as f:
        content = f.read()

    # 1. Target the PMU_STATUS register (0x00) again
    content = re.sub(
        r'uint8_t regs\[2\] = \{PMIC_REG_BAT_PERCENT, PMIC_REG_CHG_STATUS\};.*',
        'uint8_t regs[2] = {PMIC_REG_BAT_PERCENT, PMIC_REG_PMU_STATUS}; // ARCHITECT FIX: Target VBUS Status (0x00)',
        content
    )
    
    # 2. Mask Bit 4 (0x10) for VBUS Presence
    content = re.sub(
        r'global_state\.is_charging = \(\(vals\[1\] & 0x60\) != 0\);.*',
        'global_state.is_charging = ((vals[1] & 0x10) != 0); // ARCHITECT FIX: Mask Bit 4 (0x10) to detect active USB power',
        content
    )

    with open(pm_path, 'w') as f:
        f.write(content)
    print("Patched: power_manager.cpp (Locked charging telemetry to VBUS physical presence)")

if __name__ == "__main__":
    lock_vbus_telemetry()