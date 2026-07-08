#include "power_manager.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "hal_imu.h"
#include "eskf_fusion.h"
#include "ui_render.h"
#include "bsp/m5stack_core_s3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN_EXECUTOR";

extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    bsp_i2c_init();
    // Manually wake AXP2101 (PMIC) and AW9523B
    uint8_t pmic_data[2];
    pmic_data[0] = 0x90; pmic_data[1] = 0x8E; i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x34, pmic_data, 2, 100);
    pmic_data[0] = 0x92; pmic_data[1] = 0x1D; i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x34, pmic_data, 2, 100);
    pmic_data[0] = 0x93; pmic_data[1] = 0x1D; i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x34, pmic_data, 2, 100);
    pmic_data[0] = 0x99; pmic_data[1] = 0x1D; i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x34, pmic_data, 2, 100);
    vTaskDelay(pdMS_TO_TICKS(50));
    pmic_data[0] = 0x12; pmic_data[1] = 0x00; i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, pmic_data, 2, 100);
    pmic_data[0] = 0x04; pmic_data[1] = 0x00; i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, pmic_data, 2, 100);
    pmic_data[0] = 0x05; pmic_data[1] = 0x00; i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, pmic_data, 2, 100);
    pmic_data[0] = 0x02; pmic_data[1] = 0xFF; i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, pmic_data, 2, 100);
    pmic_data[0] = 0x03; pmic_data[1] = 0xFF; i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, pmic_data, 2, 100);
    ui_render_init();
    power_manager_init(); // CRITICAL: Ignite Background Power Daemon
    imu_hal_init();
    eskf_fusion_init();

    ESP_LOGI(TAG, "Architecture initialized. Starting multiplexed data pipeline...");

    while (1) {
        imu_9dof_data_t sensor_data;
        
        if (imu_hal_read_9dof(&sensor_data) == ESP_OK) {
            eskf_fusion_queue_data(&sensor_data);
        }
        
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}
