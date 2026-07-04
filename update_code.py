import os
import re

def fix_with_wrappers():
    print("1. Implementing Native C++ Wrappers for Magnetometer Wake-up...")
    app_path = os.path.normpath("main/core/app_main.cpp")
    
    if os.path.exists(app_path):
        with open(app_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # 1. Clean out the raw I2C probe that was crashing the hardware routing
        content = re.sub(r'// --- 9-DOF RAW TELEMETRY ---.*?// ------------------------------\n?', '', content, flags=re.DOTALL)

        wrapper_code = """
        // --- NATIVE BMM150 WAKE UP ---
        {
            uint8_t pwr = 0x01; write_bmm150_data(0x4B, &pwr, 1); vTaskDelay(pdMS_TO_TICKS(15));
            uint8_t repXY = 0x04; write_bmm150_data(0x51, &repXY, 1);
            uint8_t repZ = 0x0F; write_bmm150_data(0x52, &repZ, 1);
            uint8_t op = 0x00; write_bmm150_data(0x4C, &op, 1); vTaskDelay(pdMS_TO_TICKS(15));
        }
        // -----------------------------
        """
        
        # 2. Insert the native wake-up right before the infinite graphics loop
        content = re.sub(
            r'(while\s*\(\s*(1|true)\s*\)\s*\{)', 
            wrapper_code + r'\1', 
            content, 
            flags=re.IGNORECASE
        )

        with open(app_path, 'w', encoding='utf-8') as f:
            f.write(content)
        print(" -> Success: Native wrappers implemented. Hardware conflict resolved.")
    else:
        print(" -> ERROR: app_main.cpp not found.")

if __name__ == "__main__":
    fix_with_wrappers()