import os

def force_charging_floodgates():
    # 1. Update the Header File
    const_path = "main/core/pmic_constants.h"
    if os.path.exists(const_path):
        with open(const_path, 'r') as f:
            content = f.read()
        
        if "PMIC_REG_VBUS_LIMIT" not in content:
            content += "\n// --- CHARGING REGISTERS ---\n"
            content += "#define PMIC_REG_VBUS_LIMIT    0x16\n"
            content += "#define PMIC_REG_CHG_CTRL      0x18\n"
            with open(const_path, 'w') as f:
                f.write(content)
            print("Patched: pmic_constants.h (Injected VBUS/Charging definitions)")

    # 2. Update the Power Manager Daemon
    pm_path = "main/core/power_manager.cpp"
    if os.path.exists(pm_path):
        with open(pm_path, 'r') as f:
            content = f.read()

        floodgate_logic = """
// Architect Helper: Maximize USB-C Input and Battery Charging
static void open_charging_floodgates() {
    uint8_t reg_vbus = PMIC_REG_VBUS_LIMIT;
    uint8_t val_vbus = 0;
    
    // Set VBUS Input Limit to 1500mA (1.5A) - Overrides BSP defaults
    if (i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, &reg_vbus, 1, &val_vbus, 1, 100) == ESP_OK) {
        val_vbus = (val_vbus & 0xF8) | 0x04; // 0x04 = 1.5A limit
        uint8_t write_data[2] = {reg_vbus, val_vbus};
        i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, write_data, 2, 100);
    }

    // Force Charge Enable Bit
    uint8_t reg_chg = PMIC_REG_CHG_CTRL;
    uint8_t val_chg = 0;
    if (i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, &reg_chg, 1, &val_chg, 1, 100) == ESP_OK) {
        val_chg = val_chg | 0x02; // Bit 1 enables charging
        uint8_t write_data[2] = {reg_chg, val_chg};
        i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, write_data, 2, 100);
    }
    
    ESP_LOGI(TAG, "Hardware Overridden: VBUS Limit expanded to 1.5A. Charging forcefully enabled.");
}
"""
        if "open_charging_floodgates" not in content:
            # Inject the function definition right before power_manager_task
            content = content.replace("void power_manager_task(void *pvParameters) {", floodgate_logic + "\nvoid power_manager_task(void *pvParameters) {")
            
            # Inject the function call right after sever_extraneous_hardware()
            content = content.replace("    sever_extraneous_hardware();", "    sever_extraneous_hardware();\n    open_charging_floodgates();")
            
            with open(pm_path, 'w') as f:
                f.write(content)
            print("Patched: power_manager.cpp (Injected VBUS 1.5A limit override and charge enabler)")
        else:
            print("Charging floodgates already exist in power_manager.cpp.")

if __name__ == "__main__":
    force_charging_floodgates()