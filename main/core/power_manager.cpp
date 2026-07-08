#include "esp_log.h"
#include "power_manager.h"
#include "pmic_constants.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "bsp/m5stack_core_s3.h"
#include "../fusion/eskf_fusion.h"

static const char *TAG = "POWER_DAEMON";

static uint32_t idle_ticks = 0;
static int backlight_state = 2; // 2=ON, 1=DIM, 0=OFF

// Architect Helper: Safely wrap I2C writes with error tracking
static void set_backlight_voltage(uint8_t voltage_hex) {
    uint8_t pmic_data[2] = {PMIC_REG_DLDO1_CTRL, voltage_hex};
    esp_err_t err = i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, pmic_data, 2, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C Write Failed (0x%X). PMIC unresponsive.", err);
    }
}

void power_manager_task(void *pvParameters) {
    ESP_LOGI(TAG, "Power Manager Daemon Booted on Core 0.");
    uint32_t tick_count = 0;
    while(1) {
        bool moving = global_state.is_moving;

        // --- SLEEP STATE MACHINE (10Hz) ---
        if (moving) {
            idle_ticks = 0;
            if (backlight_state != 2) {
                set_backlight_voltage(PMIC_DLDO1_3V4_MAX);
                backlight_state = 2;
                ESP_LOGI(TAG, "Kinematic Disturbance Detected: Screen Restored (100%%)");
            }
        } else {
            if (idle_ticks < 3000) idle_ticks++;
            
            if (idle_ticks == 100 && backlight_state == 2) { // 10s
                set_backlight_voltage(PMIC_DLDO1_2V7_DIM);
                backlight_state = 1;
                ESP_LOGI(TAG, "10s Idle Threshold Met: Screen Dimmed (10%%)");
            } else if (idle_ticks == 3000 && backlight_state == 1) { // 300s
                set_backlight_voltage(PMIC_DLDO1_0V5_OFF);
                backlight_state = 0;
                ESP_LOGI(TAG, "300s Idle Threshold Met: Screen Off (0%%), Graphics Halted.");
            }
        }

        // --- E-GAUGE POLLING (1Hz) ---
        if (tick_count % 10 == 0) {
            uint8_t reg_pct = PMIC_REG_BAT_PERCENT; 
            uint8_t pct_val = 0;
            esp_err_t err = i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, &reg_pct, 1, &pct_val, 1, 100);
            
            if (err == ESP_OK) {
                if (pct_val <= 100) global_state.pmic_percentage = (int)pct_val;
            } else {
                ESP_LOGE(TAG, "Failed to read E-Gauge (0x%X).", err);
            }
        }

        global_state.is_sleeping = (backlight_state == 0);
        tick_count++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void power_manager_init(void) {
    xTaskCreatePinnedToCore(power_manager_task, "power_daemon", 4096, NULL, 2, NULL, 0);
}
