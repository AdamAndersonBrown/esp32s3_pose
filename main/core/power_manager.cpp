#include "esp_log.h"
#include "power_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "bsp/m5stack_core_s3.h"
#include "../fusion/eskf_fusion.h" // For blackboard access

static uint32_t idle_ticks = 0;
static int backlight_state = 2; // 2=ON, 1=DIM, 0=OFF

static const char *TAG = "POWER_DAEMON";

void power_manager_task(void *pvParameters) {
    ESP_LOGI(TAG, "Power Manager Daemon Booted on Core 0.");
    uint32_t tick_count = 0;
    while(1) {
        bool moving = global_state.is_moving;

        // --- SLEEP STATE MACHINE (10Hz) ---
        if (moving) {
            idle_ticks = 0;
            if (backlight_state != 2) {
                uint8_t pmic_data[2] = {0x99, 0x1D}; // 3.4V Max
                i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x34, pmic_data, 2, 100);
                backlight_state = 2;
                ESP_LOGI(TAG, "Kinematic Disturbance Detected: Screen Restored (100%%)");
            }
        } else {
            if (idle_ticks < 3000) idle_ticks++;
            
            if (idle_ticks == 100 && backlight_state == 2) { // 10s
                uint8_t pmic_data[2] = {0x99, 0x16}; // 2.7V Dim
                i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x34, pmic_data, 2, 100);
                backlight_state = 1;
                ESP_LOGI(TAG, "10s Idle Threshold Met: Screen Dimmed (10%%)");
            } else if (idle_ticks == 3000 && backlight_state == 1) { // 300s
                uint8_t pmic_data[2] = {0x99, 0x00}; // 0.5V Off
                i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x34, pmic_data, 2, 100);
                backlight_state = 0;
                ESP_LOGI(TAG, "300s Idle Threshold Met: Screen Off (0%%), Graphics Halted.");
            }
        }

        // --- E-GAUGE POLLING (1Hz) ---
        if (tick_count % 10 == 0) {
            uint8_t reg_pct = 0xA4; uint8_t pct_val = 0;
            if (i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, 0x34, &reg_pct, 1, &pct_val, 1, 100) == ESP_OK) {
                if (pct_val <= 100) global_state.pmic_percentage = (int)pct_val;
            }
        }

        global_state.is_sleeping = (backlight_state == 0);
        tick_count++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void power_manager_init(void) {
    // Run the daemon in the background on CPU 0
    xTaskCreatePinnedToCore(power_manager_task, "power_daemon", 4096, NULL, 2, NULL, 0);
}
