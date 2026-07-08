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

void power_manager_task(void *pvParameters) {
    ESP_LOGI(TAG, "Power Manager Daemon Booted on Core 0.");
    sever_extraneous_hardware();
    open_charging_floodgates();
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
            if (idle_ticks < 1000) idle_ticks++;
            
            if (idle_ticks == 100 && backlight_state == 2) { // 10s
                set_backlight_voltage(PMIC_DLDO1_2V7_DIM);
                backlight_state = 1;
                ESP_LOGI(TAG, "10s Idle Threshold Met: Screen Dimmed (10%%)");
            } else if (idle_ticks == 600 && backlight_state == 1) { // 300s
                set_backlight_voltage(PMIC_DLDO1_0V5_OFF);
                backlight_state = 0;
                ESP_LOGI(TAG, "60s Idle Threshold Met: Screen Off (0%%), Graphics Halted.");
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

        
        // --- HARDWARE AUDIT & ENFORCEMENT (Every 5 Seconds) ---
        if (tick_count % 50 == 0) {
            uint8_t regs[4] = {PMIC_REG_PMU_STATUS, PMIC_REG_CHG_STATUS, PMIC_REG_VBUS_LIMIT, PMIC_REG_CHG_CTRL};
            uint8_t vals[4] = {0, 0, 0, 0};
            
            for(int i=0; i<4; i++) {
                i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, &regs[i], 1, &vals[i], 1, 100);
            }

            ESP_LOGI(TAG, "PMIC AUDIT | VBUS Limit Reg: 0x%02X | Chg Ctrl Reg: 0x%02X | PMU Stat: 0x%02X | Chg Stat: 0x%02X", vals[2], vals[3], vals[0], vals[1]);

            // VBUS Limit Check: Ensure the bottom 3 bits are still 0x04 (1.5A)
            if ((vals[2] & 0x07) != 0x04) {
                ESP_LOGW(TAG, "Hardware override dropped! Re-opening charging floodgates...");
                open_charging_floodgates();
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
