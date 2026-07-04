import os
import re

def nuke_the_ghost():
    print("1. Restoring monolithic upload and injecting hardware reset...")
    app_path = os.path.normpath("main/core/app_main.cpp")
    
    if os.path.exists(app_path):
        with open(app_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # 1. Clean out the manual chunking block and restore the standard upload
        chunk_pattern = r'esp_err_t err = ESP_OK;\s*// --- BULLETPROOF CHUNKED MICROCODE UPLOAD ---.*?// --------------------------------------------'
        content = re.sub(
            chunk_pattern, 
            'esp_err_t err = i2c_master_write_to_device(I2C_NUM_1, 0x69, temp_data, 1 + length, 1000);', 
            content, 
            flags=re.DOTALL
        )

        # 2. Inject the hardware reset command directly after the I2C port boots
        if "0x7E, 0xB6" not in content:
            reset_code = """i2c_driver_install(I2C_NUM_1, i2c_conf.mode, 0, 0, 0);

        // --- BMI270 SILICON RESET ---
        uint8_t reset_cmd[2] = {0x7E, 0xB6};
        i2c_master_write_to_device(I2C_NUM_1, 0x69, reset_cmd, 2, 1000);
        vTaskDelay(pdMS_TO_TICKS(50)); // 50ms absolute boot delay to clear silicon
        // ----------------------------"""
            content = content.replace("i2c_driver_install(I2C_NUM_1, i2c_conf.mode, 0, 0, 0);", reset_code)

        with open(app_path, 'w', encoding='utf-8') as f:
            f.write(content)
        print(" -> Success: Ghost in the machine neutralized.")
    else:
        print(" -> ERROR: Could not find app_main.cpp")

if __name__ == "__main__":
    nuke_the_ghost()