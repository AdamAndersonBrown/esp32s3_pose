#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"

static inline void cores3_hardware_init(void) {
    // 1. ANTI-BRICK DELAY
    ESP_LOGW("HARDWARE", "Executing 3000ms safety delay...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    // 2. SETUP I2C0 (PMIC & GPIO Expander)
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 12,
        .scl_io_num = 11,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    i2c_param_config(I2C_NUM_0, &conf);
    i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);

    uint8_t data[2];
    
    // 3. AXP2101 PMIC: Enable ALDO2 (LCD Logic), ALDO3 (IMU), ALDO4 (Cam), DLDO1 (Backlight)
    data[0] = 0x90; data[1] = 0x8E; 
    i2c_master_write_to_device(I2C_NUM_0, 0x34, data, 2, 100);

    // 4. AXP2101: Set Voltages to 3.3V
    data[0] = 0x92; data[1] = 0x1D; i2c_master_write_to_device(I2C_NUM_0, 0x34, data, 2, 100);
    data[0] = 0x93; data[1] = 0x1D; i2c_master_write_to_device(I2C_NUM_0, 0x34, data, 2, 100);
    data[0] = 0x99; data[1] = 0x1D; i2c_master_write_to_device(I2C_NUM_0, 0x34, data, 2, 100);

    vTaskDelay(pdMS_TO_TICKS(50));

    // 5. AW9523B GPIO Expander: Release Hardware Resets (Screen & Peripherals)
    data[0] = 0x12; data[1] = 0x00; i2c_master_write_to_device(I2C_NUM_0, 0x58, data, 2, 100);
    data[0] = 0x04; data[1] = 0x00; i2c_master_write_to_device(I2C_NUM_0, 0x58, data, 2, 100);
    data[0] = 0x05; data[1] = 0x00; i2c_master_write_to_device(I2C_NUM_0, 0x58, data, 2, 100);
    data[0] = 0x02; data[1] = 0xFF; i2c_master_write_to_device(I2C_NUM_0, 0x58, data, 2, 100);
    data[0] = 0x03; data[1] = 0xFF; i2c_master_write_to_device(I2C_NUM_0, 0x58, data, 2, 100);
    
    ESP_LOGI("HARDWARE", "CoreS3 Power Rails and Safety Delays Armed.");
}
