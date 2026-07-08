import os
import re

def sever_unused_silicon():
    pm_path = "main/core/power_manager.cpp"
    if not os.path.exists(pm_path):
        print(f"ERROR: {pm_path} not found.")
        return

    with open(pm_path, 'r') as f:
        content = f.read()

    # The hardware excision function
    excision_logic = """
// Architect Helper: Kill unused CoreS3 Silicon (Camera & Audio Amp)
static void sever_extraneous_hardware() {
    uint8_t aldo_reg = 0x92; // AXP2101 ALDO1-4 ON/OFF Register
    uint8_t aldo_val = 0;
    
    // Read current LDO states
    if (i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, &aldo_reg, 1, &aldo_val, 1, 100) == ESP_OK) {
        // CoreS3 Map: ALDO1 (Cam 1.8V), ALDO2 (Cam 2.8V), ALDO3 (Audio 3.3V)
        // We mask off bits 0, 1, and 2 to physically sever power to these chips.
        uint8_t optimized_val = aldo_val & ~0x07; 
        
        if (aldo_val != optimized_val) {
            uint8_t pmic_data[2] = {aldo_reg, optimized_val};
            i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, pmic_data, 2, 100);
            ESP_LOGI(TAG, "Hardware Severed: Camera and Audio Amplifier power rails disabled.");
        }
    }
}
"""

    if "sever_extraneous_hardware" not in content:
        # Inject the function directly above the task definition
        content = content.replace("void power_manager_task(void *pvParameters) {", excision_logic + "\nvoid power_manager_task(void *pvParameters) {")
        
        # Call the function exactly once during the daemon boot
        content = content.replace('ESP_LOGI(TAG, "Power Manager Daemon Booted on Core 0.");', 
                                  'ESP_LOGI(TAG, "Power Manager Daemon Booted on Core 0.");\n    sever_extraneous_hardware();')
        
        with open(pm_path, 'w') as f:
            f.write(content)
        print("Patched: power_manager.cpp (Injected ALDO power rail severance for unused silicon)")
    else:
        print("Hardware severance already implemented.")

if __name__ == "__main__":
    sever_unused_silicon()