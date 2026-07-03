import os

def patch_sensor_hub():
    print("Initiating surgical patch: BMI270 Sensor Hub Auxiliary Routing...")

    app_main_c = """#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "eskf_fusion.h"
#include "driver/i2c.h"
#include "hardware/cores3_hardware.h"

extern void display_manager_init(void);
extern void display_manager_fill_screen(uint16_t color);
extern void display_manager_flush(void);
extern void render_world_locked_cube(float qw, float qx, float qy, float qz);

static const char *TAG = "AHRS_MAIN";

void fusion_and_render_task(void *pvParameters) {
    float current_q[4];
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while(1) {
        eskf_get_quaternion(current_q);
        display_manager_fill_screen(0x0000); 
        render_world_locked_cube(current_q[0], current_q[1], current_q[2], current_q[3]);
        display_manager_flush();
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(16));
    }
}

void bmi270_write_register(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    i2c_master_write_to_device(I2C_NUM_0, 0x69, data, 2, 100);
}

void imu_polling_task(void *pvParameters) {
    ESP_LOGI(TAG, "Configuring BMI270 Sensor Hub & Auxiliary Interface...");
    
    // 1. Disable advanced power save to access registers
    bmi270_write_register(0x7C, 0x00); 
    vTaskDelay(pdMS_TO_TICKS(10));

    // 2. Setup Mode: Enable manual auxiliary interface (aux_manual_en = 1, aux_en = 0)
    bmi270_write_register(0x44, 0x80); 
    vTaskDelay(pdMS_TO_TICKS(10));

    // 3. Awake BMM150 via indirect addressing (Target reg 0x4B, Payload 0x01)
    bmi270_write_register(0x4F, 0x4B); // AUX_WR_ADDR
    bmi270_write_register(0x50, 0x01); // AUX_WR_DATA
    vTaskDelay(pdMS_TO_TICKS(10));

    // 4. Data Mode: Enable autonomous readout (aux_manual_en = 0, aux_en = 1)
    bmi270_write_register(0x44, 0x01); 

    // 5. Set BMI270 to read starting from BMM150 data register 0x42
    bmi270_write_register(0x4D, 0x42); // AUX_RD_ADDR

    // 6. Enable Accel, Gyro, and Aux in Power Control
    bmi270_write_register(0x7D, 0x0F); 
    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t raw[12];
    int log_divider = 0;

    ESP_LOGI(TAG, "Sensor Hub Configured. Streaming 9-DoF Data...");

    while(1) {
        uint8_t reg = 0x0C; // BMI270 Data Registers Start
        if (i2c_master_write_read_device(I2C_NUM_0, 0x69, &reg, 1, raw, 12, 100) == ESP_OK) {
            int16_t ax = (raw[1] << 8) | raw[0];
            int16_t ay = (raw[3] << 8) | raw[2];
            int16_t az = (raw[5] << 8) | raw[4];
            int16_t gx = (raw[7] << 8) | raw[6];
            int16_t gy = (raw[9] << 8) | raw[8];
            int16_t gz = (raw[11] << 8) | raw[10];

            if (++log_divider >= 10) {
                ESP_LOGI("9-DOF", "A:[%6d, %6d, %6d] G:[%6d, %6d, %6d]", ax, ay, az, gx, gy, gz);
                log_divider = 0;
            }

            // Push active data to the math engine
            eskf_predict(gx * 0.0010642f, gy * 0.0010642f, gz * 0.0010642f, 
                         ax / 4096.0f, ay / 4096.0f, az / 4096.0f, 0.01f); 
        }
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void app_main(void) {
    cores3_hardware_init(); // Incorporates the 3s delay & PMIC safety we built

    display_manager_init();
    eskf_init();

    xTaskCreatePinnedToCore(imu_polling_task, "imu_poll", 4096, NULL, 10, NULL, 0);
    xTaskCreatePinnedToCore(fusion_and_render_task, "fusion_render", 8192, NULL, 5, NULL, 1);
}
"""
    with open("main/core/app_main.c", "w") as f:
        f.write(app_main_c)
    print("Patched: app_main.c (Sensor Hub Routing Enabled)")

if __name__ == "__main__":
    patch_sensor_hub()