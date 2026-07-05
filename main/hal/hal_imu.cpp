#include "hal_imu.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "sensor_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/m5stack_core_s3.h"

static const char *TAG = "HAL_IMU";
static bool imu_initialized = false;

// ARCHITECT FIX: Native BSP Routing with Strict C++ Casting
#ifndef BSP_I2C_NUM
#define IMU_I2C_PORT I2C_NUM_0
#else
#define IMU_I2C_PORT (i2c_port_t)BSP_I2C_NUM
#endif

uint8_t i2c_read_buffer[1024];
uint8_t i2c_write_buffer[1024];

#define BMI270_IF_CONF 0x6B
#define BMI270_AUX_DEV_ID 0x4B
#define BMI270_PWR_CONF 0x7C
#define BMI270_PWR_CTRL 0x7D
#define BMI270_CMD 0x7E
#define BMI270_AUX_IF_CONFIG 0x4C
#define BMI270_AUX_READ_ADDR 0x4D
#define BMI270_AUX_WRITE_ADDR 0x4E
#define BMI270_AUX_WRITE_DATA 0x4F
#define BMI270_AUX_STATUS          0x03
#define BMI270_AUX_DATA0           0x04
#define BMI270_ACC_DATA0           0x0C
#define BMI270_GYR_DATA0           0x12
#define BMI270_ACC_CONF            0x40
#define BMI270_ACC_RANGE           0x41
#define BMI270_GYR_CONF            0x42
#define BMI270_GYR_RANGE           0x43
#define BMI270_AUX_CONF            0x44
#define BMI270_INIT_CTRL           0x59
#define BMI270_INIT_DATA           0x5e
#define BMI270_INTERNAL_STATUS     0x21
#define BMM150_REG_POWER_CONTROL   0x4B
#define BMM150_DATA0               0x42

extern "C" uint8_t bmi270_context_config_file[];
extern "C" const int bmi270_context_config_file_size;

esp_err_t read_bmm150_data(uint8_t addr, uint8_t *data, int length) {
    i2c_write_buffer[0] = BMI270_AUX_READ_ADDR;
    i2c_write_buffer[1] = addr;
    esp_err_t err = i2c_master_write_to_device(IMU_I2C_PORT, 0x69, i2c_write_buffer, 2, 1000);
    i2c_write_buffer[0] = BMI270_AUX_STATUS;
    err = i2c_master_write_read_device(IMU_I2C_PORT, 0x69, i2c_write_buffer, 1, data, length, 1000);
    i2c_write_buffer[0] = BMI270_AUX_DATA0;
    err = i2c_master_write_read_device(IMU_I2C_PORT, 0x69, i2c_write_buffer, 1, data, length, 1000);
    return err;
}

esp_err_t write_bmm150_data(uint8_t addr, uint8_t *data, int length) {
    i2c_write_buffer[0] = BMI270_AUX_WRITE_DATA;
    i2c_write_buffer[1] = data[0];
    esp_err_t err = i2c_master_write_to_device(IMU_I2C_PORT, 0x69, i2c_write_buffer, 2, 1000);
    i2c_write_buffer[0] = BMI270_AUX_WRITE_ADDR;
    i2c_write_buffer[1] = addr;
    err = i2c_master_write_to_device(IMU_I2C_PORT, 0x69, i2c_write_buffer, 2, 1000);
    vTaskDelay(pdMS_TO_TICKS(5));
    return err;
}

esp_err_t write_bmi270_data(uint8_t addr, uint8_t *data, int length) {
    if (length < 32) {
        i2c_write_buffer[0] = addr;
        for (size_t i = 0; i < length; i++) i2c_write_buffer[1 + i] = data[i];
        return i2c_master_write_to_device(IMU_I2C_PORT, 0x69, i2c_write_buffer, 1 + length, 1000);
    }
    uint8_t *temp_data = (uint8_t *)malloc(length + 4);
    for (size_t i = 0; i < length; i++) temp_data[1 + i] = data[i];
    temp_data[0] = addr;
    esp_err_t err = i2c_master_write_to_device(IMU_I2C_PORT, 0x69, temp_data, 1 + length, 1000);
    free(temp_data);
    return err;
}

esp_err_t read_bmi270_data(uint8_t addr, uint8_t *data, int length) {
    i2c_write_buffer[0] = addr;
    return i2c_master_write_read_device(IMU_I2C_PORT, 0x69, i2c_write_buffer, 1, data, length, 1000);
}

esp_err_t write_bmi270_reg(uint8_t addr, uint8_t data) {
    i2c_write_buffer[0] = addr;
    i2c_write_buffer[1] = data;
    return i2c_master_write_to_device(IMU_I2C_PORT, 0x69, i2c_write_buffer, 2, 1000);
}

esp_err_t imu_hal_init(void) {
    ESP_LOGI(TAG, "Initializing 9-DoF Hardware Abstraction Layer on BSP I2C Bus...");

    // ARCHITECT FIX: NO MORE HIJACKING.
    // We rely entirely on the M5Stack BSP to have initialized the I2C bus.
    // The ESP-IDF legacy I2C driver will natively handle the mutex between our IMU and the FT3267 touch chip.

    uint8_t reset_cmd[2] = {0x7E, 0xB6};
    i2c_master_write_to_device(IMU_I2C_PORT, 0x69, reset_cmd, 2, 1000);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    write_bmi270_reg(BMI270_IF_CONF, 0x20);
    write_bmi270_reg(BMI270_PWR_CTRL, 0x0f);
    write_bmi270_reg(BMI270_PWR_CONF, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    write_bmi270_reg(BMI270_INIT_CTRL, 0x00);
    write_bmi270_data(BMI270_INIT_DATA, bmi270_context_config_file, bmi270_context_config_file_size);
    write_bmi270_reg(BMI270_INIT_CTRL, 0x01);

    write_bmi270_reg(BMI270_PWR_CTRL, 0x0f);
    write_bmi270_reg(BMI270_ACC_CONF, 0xA6);
    write_bmi270_reg(BMI270_GYR_CONF, 0xA6);
    write_bmi270_reg(BMI270_PWR_CONF, 0x02);
    write_bmi270_reg(BMI270_AUX_CONF, 0x07);
    write_bmi270_reg(BMI270_ACC_RANGE, 0x03);
    write_bmi270_reg(BMI270_GYR_RANGE, 0x00);

    write_bmi270_reg(BMI270_IF_CONF, 0x20);
    vTaskDelay(pdMS_TO_TICKS(5));

    write_bmi270_reg(BMI270_AUX_DEV_ID, 0x20);
    write_bmi270_reg(BMI270_AUX_IF_CONFIG, 0x80);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t pwr_ctrl = 0x01; write_bmm150_data(BMM150_REG_POWER_CONTROL, &pwr_ctrl, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t rep_xy = 0x17; write_bmm150_data(0x51, &rep_xy, 1);
    uint8_t rep_z = 0x52; write_bmm150_data(0x52, &rep_z, 1);

    uint8_t op_mode = 0x00; write_bmm150_data(0x4C, &op_mode, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    write_bmi270_reg(BMI270_IF_CONF, 0x00);
    write_bmi270_reg(BMI270_AUX_READ_ADDR, BMM150_DATA0);
    write_bmi270_reg(BMI270_IF_CONF, 0x20);
    write_bmi270_reg(BMI270_AUX_IF_CONFIG, 0x03);

    ESP_LOGI(TAG, "BMI270 & BMM150 initialization is done");
    imu_initialized = true;
    return ESP_OK;
}

esp_err_t imu_hal_read_9dof(imu_9dof_data_t *data) {
    if (!data || !imu_initialized) return ESP_ERR_INVALID_STATE;

    int16_t sensors_data[32];
    esp_err_t err = read_bmi270_data(BMI270_AUX_DATA0, (uint8_t *)sensors_data, 20);
    if (err != ESP_OK) return err;

    BodyVectors body = stage1_hal_transform(sensors_data);
    
    data->acc_x = body.accel[0]; data->acc_y = body.accel[1]; data->acc_z = body.accel[2];
    data->gyr_x = body.gyro[0]; data->gyr_y = body.gyro[1]; data->gyr_z = body.gyro[2];
    data->mag_x = body.mag[0]; data->mag_y = body.mag[1]; data->mag_z = body.mag[2];

    static float prev_mag[3] = {0, 0, 0};
    static uint8_t deadlock_counter = 0;
    
    if (body.mag[0] == prev_mag[0] && body.mag[1] == prev_mag[1] && body.mag[2] == prev_mag[2]) {
        if (deadlock_counter < 255) deadlock_counter++;
        if (deadlock_counter > 50) data->mag_valid = false;
    } else {
        deadlock_counter = 0;
        data->mag_valid = true;
    }
    
    prev_mag[0] = body.mag[0]; prev_mag[1] = body.mag[1]; prev_mag[2] = body.mag[2];
    return ESP_OK;
}



// ARCHITECT FIX: Bare-Metal Touch Polling & AW9523B Reset
bool imu_hal_read_touch(int16_t *x, int16_t *y) {
    static bool touch_hw_awake = false;
    if (!touch_hw_awake) {
        // AW9523B Expander (0x58). Pin P0_0 controls the FT6336U Touch Reset.
        uint8_t conf = 0;
        uint8_t reg_conf = 0x04; // P0 Configuration Register
        if (i2c_master_write_read_device(IMU_I2C_PORT, 0x58, &reg_conf, 1, &conf, 1, 1000) == ESP_OK) {
            conf &= ~0x01; // Set P0_0 to Output
            uint8_t write_conf[2] = {0x04, conf};
            i2c_master_write_to_device(IMU_I2C_PORT, 0x58, write_conf, 2, 1000);
        }
        
        uint8_t out = 0;
        uint8_t reg_out = 0x02; // P0 Output Data Register
        if (i2c_master_write_read_device(IMU_I2C_PORT, 0x58, &reg_out, 1, &out, 1, 1000) == ESP_OK) {
            out |= 0x01; // Pull P0_0 HIGH to release FT6336U from Reset
            uint8_t write_out[2] = {0x02, out};
            i2c_master_write_to_device(IMU_I2C_PORT, 0x58, write_out, 2, 1000);
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); // Give the Touch IC 50ms to boot
        touch_hw_awake = true;
    }

    uint8_t data[5];
    uint8_t reg = 0x02; // FT6336U TD_STATUS Register
    esp_err_t err = i2c_master_write_read_device(IMU_I2C_PORT, 0x38, &reg, 1, data, 5, 1000);
    
    if (err == ESP_OK && (data[0] & 0x0F) > 0) { // If point count > 0
        *x = ((data[1] & 0x0F) << 8) | data[2];
        *y = ((data[3] & 0x0F) << 8) | data[4];
        return true;
    }
    return false;
}
