import os

def revive_i2c():
    print("1. Injecting Legacy I2C Driver for the IMU...")
    app_path = os.path.normpath("main/core/app_main.cpp")
    
    if os.path.exists(app_path):
        with open(app_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # Swap all I2C Port 0 read/write commands over to Port 1
        content = content.replace("I2C_NUM_0", "I2C_NUM_1")

        init_code = """
        // -- HARDWARE HIJACK: Map internal I2C pins to Legacy Port 1 --
        i2c_config_t i2c_conf = {};
        i2c_conf.mode = I2C_MODE_MASTER;
        i2c_conf.sda_io_num = 12; // CoreS3 Internal SDA
        i2c_conf.scl_io_num = 11; // CoreS3 Internal SCL
        i2c_conf.sda_pullup_en = true;
        i2c_conf.scl_pullup_en = true;
        i2c_conf.master.clk_speed = 400000; // 400kHz Fast Mode
        
        i2c_param_config(I2C_NUM_1, &i2c_conf);
        i2c_driver_install(I2C_NUM_1, i2c_conf.mode, 0, 0, 0);
        // -------------------------------------------------------------
"""
        
        # Inject the initialization right before the graphics loop starts
        if "i2c_driver_install" not in content:
            content = content.replace("bsp_display_lock(0);", init_code + "\n        bsp_display_lock(0);")

        with open(app_path, 'w', encoding='utf-8') as f:
            f.write(content)
            
        print(" -> Success: Sensors mapped and matrix hijacked to Port 1.")
    else:
        print(" -> ERROR: app_main.cpp not found!")

if __name__ == "__main__":
    revive_i2c()