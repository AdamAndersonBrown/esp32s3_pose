#include "hal_touch.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/m5stack_core_s3.h" // ARCHITECT FIX: Required to map BSP_I2C_NUM

#ifndef BSP_I2C_NUM
#define TOUCH_I2C_PORT I2C_NUM_0
#else
#define TOUCH_I2C_PORT (i2c_port_t)BSP_I2C_NUM
#endif

bool touch_hal_read(int16_t *x, int16_t *y) {
    static bool touch_hw_awake = false;
    if (!touch_hw_awake) {
        uint8_t conf = 0;
        uint8_t reg_conf = 0x04; 
        if (i2c_master_write_read_device(TOUCH_I2C_PORT, 0x58, &reg_conf, 1, &conf, 1, 1000) == ESP_OK) {
            conf &= ~0x01; 
            uint8_t write_conf[2] = {0x04, conf};
            i2c_master_write_to_device(TOUCH_I2C_PORT, 0x58, write_conf, 2, 1000);
        }
        
        uint8_t out = 0;
        uint8_t reg_out = 0x02; 
        if (i2c_master_write_read_device(TOUCH_I2C_PORT, 0x58, &reg_out, 1, &out, 1, 1000) == ESP_OK) {
            out |= 0x01; 
            uint8_t write_out[2] = {0x02, out};
            i2c_master_write_to_device(TOUCH_I2C_PORT, 0x58, write_out, 2, 1000);
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); 
        touch_hw_awake = true;
    }

    uint8_t data[5];
    uint8_t reg = 0x02; 
    esp_err_t err = i2c_master_write_read_device(TOUCH_I2C_PORT, 0x38, &reg, 1, data, 5, 1000);
    
    if (err == ESP_OK && (data[0] & 0x0F) > 0) { 
        *x = ((data[1] & 0x0F) << 8) | data[2];
        *y = ((data[3] & 0x0F) << 8) | data[4];
        return true;
    }
    return false;
}
