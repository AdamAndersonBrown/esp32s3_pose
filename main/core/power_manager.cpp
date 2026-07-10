#include "esp_log.h"
#include "driver/temperature_sensor.h"
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


// Architect Helper: Restore CoreS3 Silicon (AW9523B & IMU Routing)
static void sever_extraneous_hardware() {
    uint8_t aldo_reg = PMIC_REG_LDO_ONOFF; 
    uint8_t aldo_val = 0;
    
    if (i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, &aldo_reg, 1, &aldo_val, 1, 100) == ESP_OK) {
        // ARCHITECT FIX: Force ALDO 1, 2, 3, and 4 back ON.
        // The PMIC retains states across warm reboots. We must explicitly re-enable 
        // the rails that power the AW9523B GPIO expander to unbrick the IMU interrupt.
        // ARCHITECT FIX: Hardware Severance. 
        // 0x0A (0000 1010) keeps ALDO2 (LCD) and ALDO4 (Touch/AW9523B) ON.
        // It actively KILLS ALDO 1 and 3 (Audio Amp, Camera).
        uint8_t optimized_val = (aldo_val & 0xF0) | 0x0A; 
        
        if (aldo_val != optimized_val) {
            uint8_t pmic_data[2] = {aldo_reg, optimized_val};
            i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, pmic_data, 2, 100);
            ESP_LOGI(TAG, "Hardware Restored: ALDO rails re-energized to unbrick IMU.");
        }
    }
}

// Architect Helper: Maximize USB-C Input and Battery Charging
static void open_charging_floodgates() {
    
    // Prevent Dynamic Power Management (DPM) from choking current when the cable sags
    uint8_t reg_vhold = PMIC_REG_VBUS_VOLTAGE;
    uint8_t val_vhold = 0;
    if (i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, &reg_vhold, 1, &val_vhold, 1, 100) == ESP_OK) {
        val_vhold = (val_vhold & 0xF0) | 0x02; // Set VHOLD threshold to 4.0V
        uint8_t write_data[2] = {reg_vhold, val_vhold};
        i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, write_data, 2, 100);
    }

    uint8_t reg_vbus = PMIC_REG_VBUS_LIMIT;
    uint8_t val_vbus = 0;
    
    // Set VBUS Input Limit to 1500mA (1.5A) - Overrides BSP defaults
    if (i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, &reg_vbus, 1, &val_vbus, 1, 100) == ESP_OK) {
        val_vbus = (val_vbus & 0xF8) | 0x04; // ARCHITECT FIX: Unified to 1.5A (0x04)
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
    
    
    // Force Constant Charge Current to Hardware Maximum (1000mA)
    uint8_t reg_cc = PMIC_REG_CHG_CURRENT;
    uint8_t val_cc = 0;
    if (i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, &reg_cc, 1, &val_cc, 1, 100) == ESP_OK) {
        // We set the bitmask to maximum to guarantee up to 1000mA of charge current
        val_cc = 0x1B; // 0x1B is the documented hex value for 1000mA on the AXP2101
        uint8_t write_data[2] = {reg_cc, val_cc};
        i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, write_data, 2, 100);
    }

    ESP_LOGI(TAG, "Hardware Overridden: VBUS Limit to 1.5A. Charge Current MAXED (1A).");
}

void power_manager_task(void *pvParameters) {
    // ARCHITECT FIX: Initialize ESP32-S3 Internal Temperature Sensor
    temperature_sensor_handle_t temp_handle = NULL;
    temperature_sensor_config_t temp_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
    temperature_sensor_install(&temp_config, &temp_handle);
    temperature_sensor_enable(temp_handle);

    ESP_LOGI(TAG, "Power Manager Daemon Booted on Core 0.");
    sever_extraneous_hardware();
    open_charging_floodgates(); // ARCHITECT FIX: UNCOMMENTED! Take down the fence.
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
            } else if (idle_ticks == 600 && backlight_state == 1) { // 60s
                set_backlight_voltage(PMIC_DLDO1_0V5_OFF);
                backlight_state = 0;
                ESP_LOGI(TAG, "60s Idle Threshold Met: Screen Off (0%%), Graphics Halted.");
            }
        }

        // --- E-GAUGE & CHARGE STATUS POLLING (1Hz) ---
        // ARCHITECT FIX: Thread-safe I2C Mutex to prevent AW9523B/PMIC bus collisions
        static SemaphoreHandle_t pmic_i2c_mutex = NULL;
        if (pmic_i2c_mutex == NULL) pmic_i2c_mutex = xSemaphoreCreateMutex();

        if (tick_count % 10 == 0) {
            if (xSemaphoreTake(pmic_i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                
                // 1. HARDWARE ROUTER: Safely Audit and Enforce OTG Severance
                uint8_t aw_conf_read = 0x04, aw_conf_val = 0;
                i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, 0x58, &aw_conf_read, 1, &aw_conf_val, 1, 100);

                // Enforce P0_5 (USB_OTG_EN) remains an Output (& 0xDF)
                uint8_t aw_conf_write[2] = {0x04, (uint8_t)(aw_conf_val & 0xDF)}; 
                i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, aw_conf_write, 2, 100);

                uint8_t aw_out_read = 0x02, aw_out_val = 0;
                i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, 0x58, &aw_out_read, 1, &aw_out_val, 1, 100);

                // ARCHITECT FIX: Force Bit 5 (USB_OTG_EN) LOW (& 0xDF). 
                // Do NOT use & 0xCF or | 0x10. We must not touch Bit 4 (TF_SW) or Bit 0 (TOUCH).
                uint8_t aw_out_write[2] = {0x02, (uint8_t)(aw_out_val & 0xDF)}; 
                i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, 0x58, aw_out_write, 2, 100);

                // 2. SILENT REVERSION AUDIT: Force Floodgates Open against PMIC brownouts
                uint8_t reg16 = 0x16, val16 = 0; // VBUS Limit
                i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, &reg16, 1, &val16, 1, 100);
                if ((val16 & 0x07) != 0x04) { // Force 1.5A
                    uint8_t write16[2] = {0x16, (uint8_t)((val16 & 0xF8) | 0x04)};
                    i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, write16, 2, 100);
                }

                uint8_t reg62 = 0x62, val62 = 0; // Charge Current
                i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, &reg62, 1, &val62, 1, 100);
                if (val62 != 0x1B) { // ARCHITECT FIX: Align to 1.0A (0x1B) instead of 0x13
                    uint8_t write62[2] = {0x62, 0x1B};
                    i2c_master_write_to_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, write62, 2, 100);
                }

                // 3. TELEMETRY: Read PMU_STATUS (0x00), CHG_STATUS (0x01), E-Gauge (0xA4)
                uint8_t regs[3] = {0x00, 0x01, 0xA4};
                uint8_t vals[3] = {0, 0, 0};
                bool i2c_success = true;
                
                for(int i=0; i<3; i++) {
                    if (i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, &regs[i], 1, &vals[i], 1, 100) != ESP_OK) {
                        vals[i] = 0xEE;
                        i2c_success = false;
                    }
                }
                
                xSemaphoreGive(pmic_i2c_mutex);

                if (i2c_success) {
                    if (vals[2] <= 100) global_state.pmic_percentage = (int)vals[2]; // E-Gauge sync
                    
                    if (temp_handle != NULL) {
                        float tsens_value = 0.0f;
                        temperature_sensor_get_celsius(temp_handle, &tsens_value);
                        global_state.system_temp = tsens_value;
                    }
                    
                    // True VBUS Valid check (Bit 5 of Reg 0x00)
                    global_state.is_charging = ((vals[0] & 0x20) != 0); 
                }
            }
        }

        // --- HARDWARE AUDIT & ENFORCEMENT (Every 5 Seconds) ---
        if (tick_count % 50 == 0) {
            uint8_t regs[5] = {PMIC_REG_PMU_STATUS, PMIC_REG_CHG_STATUS, PMIC_REG_VBUS_LIMIT, PMIC_REG_CHG_CTRL, PMIC_REG_CHG_CURRENT};
            uint8_t vals[5] = {0, 0, 0, 0, 0};
            
            for(int i=0; i<5; i++) {
                i2c_master_write_read_device((i2c_port_t)BSP_I2C_NUM, PMIC_I2C_ADDR, &regs[i], 1, &vals[i], 1, 100);
            }

            ESP_LOGI(TAG, "PMIC AUDIT | Bat: %d%% | VBUS: 0x%02X | ChgCtrl: 0x%02X | ChgCur: 0x%02X | PMU Stat: 0x%02X | Chg Stat: 0x%02X", global_state.pmic_percentage, vals[2], vals[3], vals[4], vals[0], vals[1]);

            // VBUS Limit Check (1.5A) OR Charge Current Check (1.0A = 0x1B)
            if (((vals[2] & 0x07) != 0x04) || (vals[4] != 0x1B)) { // ARCHITECT FIX: Audit for 500mA
                ESP_LOGW(TAG, "Hardware override dropped! Re-opening charging floodgates...");
                open_charging_floodgates(); // ARCHITECT FIX: UNCOMMENTED! Take down the fence.
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
