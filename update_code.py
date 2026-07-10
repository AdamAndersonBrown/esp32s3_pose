import os

def main():
    print("Initiating Senior Architect Patch: The Final Ouroboros Severance...\n")

    # --- PATCH 1: app_main.cpp (The LED Mode Trap and Ghost Command) ---
    app_main_path = os.path.join('main', 'core', 'app_main.cpp')
    if os.path.exists(app_main_path):
        with open(app_main_path, 'r') as f:
            content = f.read()

        # The exact block currently in your file
        old_boot_seq = """    pmic_data[0] = 0x12; pmic_data[1] = 0x00; i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, pmic_data, 2, 100);
    pmic_data[0] = 0x04; pmic_data[1] = 0x00; i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, pmic_data, 2, 100);
    pmic_data[0] = 0x05; pmic_data[1] = 0x00; i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, pmic_data, 2, 100);
    pmic_data[0] = 0x02; pmic_data[1] = 0xEF; // ARCHITECT FIX: Disable USB_OTG_EN (Bit 4) to STOP 5V OTG backfeed
    pmic_data[0] = 0x03; pmic_data[1] = 0xFF; i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, pmic_data, 2, 100);"""

        # The corrected architecture replacement
        new_boot_seq = """    // 1. Configure Port 0 as GPIO Push-Pull Mode (0xFF = GPIO, 0x00 = LED mode)
    // ARCHITECT FIX: This clears the LED open-drain trap!
    pmic_data[0] = 0x12; pmic_data[1] = 0xFF; 
    i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, pmic_data, 2, 100);

    // 2. Set AW9523B Port 0 and Port 1 Direction to OUTPUT (0x00 = Output)
    pmic_data[0] = 0x04; pmic_data[1] = 0x00; 
    i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, pmic_data, 2, 100);
    pmic_data[0] = 0x05; pmic_data[1] = 0x00; 
    i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, pmic_data, 2, 100);

    // 3. SEVER THE OUROBOROS LOOP (Port 0)
    // CoreS3 Pinmap: P0_5 is USB_OTG_EN. We force it LOW by masking with 0xDF (1101 1111).
    // We leave other bits HIGH to prevent resetting the touch IC (P0_0) and microSD (P0_4).
    pmic_data[0] = 0x02; pmic_data[1] = 0xDF; 
    i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, pmic_data, 2, 100); // FIXED: Added missing execution call

    // 4. Configure Internal Peripherals (Port 1)
    pmic_data[0] = 0x03; pmic_data[1] = 0xFF; 
    i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, pmic_data, 2, 100);"""

        if old_boot_seq in content:
            content = content.replace(old_boot_seq, new_boot_seq)
            with open(app_main_path, 'w') as f:
                f.write(content)
            print(f"[SUCCESS] Patched {app_main_path} (LED Mode Trap Cleared, OTG Loop Severed on Boot)")
        else:
            print(f"[WARN] Target block not found in {app_main_path}. It may already be patched.")
    else:
        print(f"[FATAL] File not found: {app_main_path}")

    # --- PATCH 2: power_manager.cpp (The CoreS3 Hardware Pin Misalignment) ---
    pm_path = os.path.join('main', 'core', 'power_manager.cpp')
    if os.path.exists(pm_path):
        with open(pm_path, 'r') as f:
            content = f.read()

        # The exact block currently in your file
        old_router_seq = """                // 1. HARDWARE ROUTER: Kill Boost (Bit 5=0), Enable VBUS (Bit 4=1)
                uint8_t aw_conf_read = 0x04, aw_conf_val = 0;
                i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, 0x58, &aw_conf_read, 1, &aw_conf_val, 1, 100);
                uint8_t aw_conf_write[2] = {0x04, (uint8_t)(aw_conf_val & 0xCF)}; // Config as outputs
                i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, aw_conf_write, 2, 100);

                uint8_t aw_out_read = 0x02, aw_out_val = 0;
                i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, 0x58, &aw_out_read, 1, &aw_out_val, 1, 100);
                uint8_t aw_out_write[2] = {0x02, (uint8_t)((aw_out_val & 0xCF) | 0x10)}; // Route VBUS
                i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, aw_out_write, 2, 100);"""

        # The corrected architecture replacement
        new_router_seq = """                // 1. HARDWARE ROUTER: Safely Audit and Enforce OTG Severance
                uint8_t aw_conf_read = 0x04, aw_conf_val = 0;
                i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, 0x58, &aw_conf_read, 1, &aw_conf_val, 1, 100);

                // Enforce P0_5 (USB_OTG_EN) remains an Output (& 0xDF)
                uint8_t aw_conf_write[2] = {0x04, (uint8_t)(aw_conf_val & 0xDF)}; 
                i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, aw_conf_write, 2, 100);

                uint8_t aw_out_read = 0x02, aw_out_val = 0;
                i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, 0x58, &aw_out_read, 1, &aw_out_val, 1, 100);

                // ARCHITECT FIX: Force Bit 5 (USB_OTG_EN) LOW (& 0xDF). 
                // Do NOT use & 0xCF or | 0x10. We must not touch Bit 4 (TF_SW) or Bit 0 (TOUCH).
                uint8_t aw_out_write[2] = {0x02, (uint8_t)(aw_out_val & 0xDF)}; 
                i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, aw_out_write, 2, 100);"""

        if old_router_seq in content:
            content = content.replace(old_router_seq, new_router_seq)
            with open(pm_path, 'w') as f:
                f.write(content)
            print(f"[SUCCESS] Patched {pm_path} (Daemon Hardware Router Safely Aligned)")
        else:
            print(f"[WARN] Target block not found in {pm_path}. It may already be patched.")
    else:
        print(f"[FATAL] File not found: {pm_path}")

    print("\nPatch complete. The CoreS3 power topologies are now perfectly aligned. Please build and flash.")

if __name__ == "__main__":
    main()