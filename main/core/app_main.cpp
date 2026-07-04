#include "driver/i2c.h"
/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "esp_log.h"
#include "sensor_hal.h"
#include "sensor_calib.h"
#include "sensor_ned.h"
#include "bsp/m5stack_core_s3.h"
#include "esp_dsp.h"
#include "cube_matrix.h"
#include "esp_logo.h"
#include "esp_text.h"
#include "graphics_support.h"
#include "image_to_3d_matrix.h"

static const char *TAG = "3d-kalman";

dspm::Mat perspective_matrix(MATRIX_SIZE, MATRIX_SIZE);

extern "C" void app_main();

#define M5_CUBE_SIDE (BSP_LCD_V_RES / 4)

// X Y Z coordinates of the cube centered to (0, 0, 0)
const float m5_cube_vectors_3d[CUBE_POINTS][MATRIX_SIZE] =
    //      X           Y           Z      W
{   {-M5_CUBE_SIDE, -M5_CUBE_SIDE, -M5_CUBE_SIDE, 1},              // -1, -1, -1
    {-M5_CUBE_SIDE, -M5_CUBE_SIDE,  M5_CUBE_SIDE, 1},              // -1, -1,  1
    {-M5_CUBE_SIDE,  M5_CUBE_SIDE, -M5_CUBE_SIDE, 1},              // -1,  1, -1
    {-M5_CUBE_SIDE,  M5_CUBE_SIDE,  M5_CUBE_SIDE, 1},              // -1,  1,  1
    { M5_CUBE_SIDE, -M5_CUBE_SIDE, -M5_CUBE_SIDE, 1},              //  1, -1, -1
    { M5_CUBE_SIDE, -M5_CUBE_SIDE,  M5_CUBE_SIDE, 1},              //  1, -1,  1
    { M5_CUBE_SIDE,  M5_CUBE_SIDE, -M5_CUBE_SIDE, 1},              //  1,  1, -1
    { M5_CUBE_SIDE,  M5_CUBE_SIDE,  M5_CUBE_SIDE, 1}               //  1,  1,  1
};

/**
 * @brief Initialize 3d image structure
 *
 * Assigns a 3d image to be displayed to the 3d image structure based on the Kconfig menu result.
 * The Kconfig menu is operated by a user
 *
 * @param image: 3d image structure
 */
#define COMPASS_POINTS 14
#define COMPASS_EDGES 16

const float compass_vectors_3d[COMPASS_POINTS][MATRIX_SIZE] = {
    // Cube Chassis (0-7)
    {-M5_CUBE_SIDE, -M5_CUBE_SIDE, -M5_CUBE_SIDE, 1},
    {-M5_CUBE_SIDE, -M5_CUBE_SIDE,  M5_CUBE_SIDE, 1},
    {-M5_CUBE_SIDE,  M5_CUBE_SIDE, -M5_CUBE_SIDE, 1},
    {-M5_CUBE_SIDE,  M5_CUBE_SIDE,  M5_CUBE_SIDE, 1},
    { M5_CUBE_SIDE, -M5_CUBE_SIDE, -M5_CUBE_SIDE, 1},
    { M5_CUBE_SIDE, -M5_CUBE_SIDE,  M5_CUBE_SIDE, 1},
    { M5_CUBE_SIDE,  M5_CUBE_SIDE, -M5_CUBE_SIDE, 1},
    { M5_CUBE_SIDE,  M5_CUBE_SIDE,  M5_CUBE_SIDE, 1},
    // Earth Tangent Plane (NESW) (8-13)
    { M5_CUBE_SIDE*1.5f, 0, 0, 1}, // 8: North (X+)
    {-M5_CUBE_SIDE*1.5f, 0, 0, 1}, // 9: South (X-)
    { 0, M5_CUBE_SIDE*1.5f, 0, 1}, // 10: East (Y+)
    { 0,-M5_CUBE_SIDE*1.5f, 0, 1}, // 11: West (Y-)
    { M5_CUBE_SIDE*1.1f, M5_CUBE_SIDE*0.3f, 0, 1}, // 12: North Arrowhead
    { M5_CUBE_SIDE*1.1f,-M5_CUBE_SIDE*0.3f, 0, 1}  // 13: North Arrowhead
};

const uint8_t compass_line_begin[COMPASS_EDGES] = {
    3, 3, 5, 5, 2, 2, 4, 4, 3, 7, 1, 5, // 12 cube edges
    8, 10,  // N-S axis, E-W axis
    8, 8    // Arrow head
};
const uint8_t compass_line_end[COMPASS_EDGES] = {
    1, 7, 7, 1, 0, 6, 6, 0, 2, 6, 0, 4, // 12 cube edges
    9, 11,  // N-S axis, E-W axis
    12, 13  // Arrow head
};

static void init_3d_matrix_struct(image_3d_matrix_t *image)
{
    // ARCHITECT FIX: Overriding primitive cube with full NESW Earth Tangent Compass Plane
    image->matrix = compass_vectors_3d;
    image->matrix_len = COMPASS_POINTS;
}

lv_style_t style_red;
lv_style_t style_blue;
lv_style_t style_green; // ARCHITECT FIX: Added for Earth Tangent Plane (NESW)
lv_display_t *display = NULL;
/**
 * @brief Initialize display
 */
lv_obj_t **objs;
lv_point_precise_t *points;


static 
#define BSP_BMI270_ADDR     0x69
#define BSP_BMM150_ADDR     0x10

// i2c read/write buffers
uint8_t i2c_read_buffer[1024];
uint8_t i2c_write_buffer[1024];

// Definitions for bmi270 registers
#define BMI270_IF_CONF 0x6B  // Auxiliary I2C
#define BMI270_AUX_DEV_ID 0x4B  // Auxiliary I2C Device address
#define BMI270_PWR_CONF 0x7C
#define BMI270_PWR_CTRL 0x7D  // Auxiliary I2C Device address
#define BMI270_CMD 0x7E // CMD
#define BMI270_AUX_IF_CONFIG 0x4C  // Auxiliary I2C configuration register
#define BMI270_AUX_READ_ADDR 0x4D    // Read from auxiliary device
#define BMI270_AUX_WRITE_ADDR 0x4E   // Write to auxiliary device
#define BMI270_AUX_WRITE_DATA 0x4F   // Write to auxiliary device
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
#define BMI270_INTERNAL_STATUS               0x21

// Definitions for bmi150 registers
#define BMM150_REG_POWER_CONTROL    0x4B
#define BMM150_SHIP_ID              0x40
#define BMM150_DATA0                0x42

// Basic functions to access bmi270 and bmm150
esp_err_t read_bmm150_data(uint8_t addr, uint8_t *data, int length)
{
    i2c_write_buffer[0] = BMI270_AUX_READ_ADDR;
    i2c_write_buffer[1] = addr;
    esp_err_t err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 2, 1000);

    i2c_write_buffer[0] = BMI270_AUX_STATUS;
    err = i2c_master_write_read_device(I2C_NUM_1, 0x69, i2c_write_buffer, 1, data, length, 1000);

    i2c_write_buffer[0] = BMI270_AUX_DATA0;
    err = i2c_master_write_read_device(I2C_NUM_1, 0x69, i2c_write_buffer, 1, data, length, 1000);
    return err;
}

esp_err_t write_bmm150_data(uint8_t addr, uint8_t *data, int length)
{
    // ARCHITECT FIX (REVERSION): Datasheet mandates data must be stored in 0x4F BEFORE writing to 0x4E.
    // Writing to AUX_WR_ADDR (0x4E) triggers the actual I2C transaction on the auxiliary bus.
    i2c_write_buffer[0] = BMI270_AUX_WRITE_DATA;
    i2c_write_buffer[1] = data[0];
    esp_err_t err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 2, 1000);

    i2c_write_buffer[0] = BMI270_AUX_WRITE_ADDR;
    i2c_write_buffer[1] = addr;
    err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 2, 1000);

    // The BMI270 requires time to process the auxiliary I2C write across the physical traces.
    vTaskDelay(pdMS_TO_TICKS(5));

    return err;
}

esp_err_t write_bmi270_data(uint8_t addr, uint8_t *data, int length)
{
    if (length < 32) {
        i2c_write_buffer[0] = addr; // ARCHITECT FIX: Route to intended register
        for (size_t i = 0; i < length; i++) {
            i2c_write_buffer[1 + i] = data[i];
        }
        esp_err_t err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 1 + length, 1000);
        return err;
    }

    uint8_t *temp_data = (uint8_t *)malloc(length + 4);
    for (size_t i = 0; i < length; i++) {
        temp_data[1 + i] = data[i];
    }
    temp_data[0] = addr;

    
        esp_err_t err = i2c_master_write_to_device(I2C_NUM_1, 0x69, temp_data, 1 + length, 1000);
        
    free(temp_data);
    return err;
}

esp_err_t read_bmi270_data(uint8_t addr, uint8_t *data, int length)
{

    i2c_write_buffer[0] = addr;
    esp_err_t err = i2c_master_write_read_device(I2C_NUM_1, 0x69, i2c_write_buffer, 1, data, length, 1000);
    return err;
}

esp_err_t write_bmi270_reg(uint8_t addr, uint8_t data)
{

    i2c_write_buffer[0] = addr;
    i2c_write_buffer[1] = data;
    esp_err_t err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 2, 1000);
    return err;
}

uint8_t read_bmi270_reg(uint8_t addr, esp_err_t *err)
{

    i2c_write_buffer[0] = addr;
    *err = i2c_master_write_read_device(I2C_NUM_1, 0x69, i2c_write_buffer, 1, &i2c_write_buffer[16], 1, 1000);
    return i2c_write_buffer[16];
}

extern "C" uint8_t bmi270_context_config_file[];
extern "C" const int bmi270_context_config_file_size;

int16_t sensors_data[32];

/**
 * @brief Initialize the application.
 *
 * This function initialize the display 3D points and chips: mbi270 and bmm150
 *
 */

static void app_init(void)
{
    lv_style_init(&style_red);
    lv_style_init(&style_blue);
    lv_style_init(&style_green);

    lv_style_set_line_color(&style_red, lv_palette_main(LV_PALETTE_RED));
    lv_style_set_line_width(&style_red, 10);
    lv_style_set_line_rounded(&style_red, false);
    
    lv_style_set_line_color(&style_blue, lv_palette_main(LV_PALETTE_BLUE));
    lv_style_set_line_width(&style_blue, 10);
    lv_style_set_line_rounded(&style_blue, false);

    // ARCHITECT FIX: Style configuration for the Compass Rose
    lv_style_set_line_color(&style_green, lv_palette_main(LV_PALETTE_GREEN));
    lv_style_set_line_width(&style_green, 12);
    lv_style_set_line_rounded(&style_green, true);

    objs = (lv_obj_t **)malloc(COMPASS_EDGES * sizeof(lv_obj_t *));
    points = (lv_point_precise_t *)malloc(COMPASS_EDGES * 2 * sizeof(lv_point_precise_t));

    for (uint8_t i = 0; i < 6; i++) {
        objs[i] = lv_line_create(lv_screen_active());
        lv_obj_add_style(objs[i], &style_red, 0);
    }
    for (uint8_t i = 6; i < 12; i++) {
        objs[i] = lv_line_create(lv_screen_active());
        lv_obj_add_style(objs[i], &style_blue, 0);
    }
    for (uint8_t i = 12; i < COMPASS_EDGES; i++) {
        objs[i] = lv_line_create(lv_screen_active());
        lv_obj_add_style(objs[i], &style_green, 0);
    }

    // Init the bmi270 and bmm150 chips
    esp_err_t err = ESP_OK;

    
    
    vTaskDelay(10);
    // Read chip ID to check the connection
    i2c_write_buffer[0] = 0x00;
    err = i2c_master_write_read_device(I2C_NUM_1, 0x69, i2c_write_buffer, 1, i2c_read_buffer, 1, 1000);
    ESP_LOGI(TAG, "bmi270 ChipID = 0x%2.2x (should be 0x24), err = %2.2x", i2c_read_buffer[0], err);

    i2c_write_buffer[0] = BMI270_IF_CONF;
    i2c_write_buffer[1] = 0x20;
    err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 2, 1000);

    i2c_write_buffer[0] = BMI270_IF_CONF;
    err = i2c_master_write_read_device(I2C_NUM_1, 0x69, i2c_write_buffer, 1, i2c_read_buffer, 1, 1000);

    i2c_write_buffer[0] = BMI270_PWR_CTRL;
    i2c_write_buffer[1] = 0x0f;
    err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 2, 1000);

    i2c_write_buffer[0] = BMI270_PWR_CTRL;
    err = i2c_master_write_read_device(I2C_NUM_1, 0x69, i2c_write_buffer, 1, i2c_read_buffer, 1, 1000);

    // ARCHITECT FIX: Deferred BMM150 setup to after microcode load.

    write_bmi270_reg(BMI270_PWR_CONF, 0);
    ESP_LOGI(TAG, "bmi270 status = %2.2x", read_bmi270_reg(BMI270_INTERNAL_STATUS, &err));
    vTaskDelay(10 / portTICK_PERIOD_MS);

    write_bmi270_reg(BMI270_INIT_CTRL, 0x00);
    err = write_bmi270_data(BMI270_INIT_DATA, bmi270_context_config_file, bmi270_context_config_file_size);
    write_bmi270_reg(BMI270_INIT_CTRL, 0x01);
    ESP_LOGI(TAG, "bmi270 status = %2.2x", read_bmi270_reg(BMI270_INTERNAL_STATUS, &err));

    write_bmi270_reg(BMI270_PWR_CTRL, 0x0f);
    write_bmi270_reg(BMI270_ACC_CONF, 0xA6);
    write_bmi270_reg(BMI270_GYR_CONF, 0xA6);
    write_bmi270_reg(BMI270_PWR_CONF, 0x02);
    write_bmi270_reg(BMI270_AUX_CONF, 0x07);
    write_bmi270_reg(BMI270_ACC_RANGE, 0x03);
    write_bmi270_reg(BMI270_GYR_RANGE, 0x00);

    // --- ARCHITECT FIX: BMM150 SETUP (AFTER MICROCODE LOAD) ---
    // Ensure auxiliary interface is ENABLED during manual setup (Bit 5 = 1)
    write_bmi270_reg(BMI270_IF_CONF, 0x20);
    vTaskDelay(pdMS_TO_TICKS(5));

    // 1. Set Target I2C Device Address (0x10 << 1 = 0x20)
    write_bmi270_reg(BMI270_AUX_DEV_ID, 0x20);

    // 2. Enter Setup Mode (manual auxiliary routing)
    write_bmi270_reg(BMI270_AUX_IF_CONFIG, 0x80);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 3. Wake BMM150 from Suspend to Sleep (Power Control = 1)
    uint8_t pwr_ctrl = 0x01;
    write_bmm150_data(BMM150_REG_POWER_CONTROL, &pwr_ctrl, 1);
    vTaskDelay(pdMS_TO_TICKS(10)); // Oscillator stabilization

    // 4. Configure High-Accuracy Repetitions (XY: 47 -> 0x17, Z: 83 -> 0x52)
    uint8_t rep_xy = 0x17; write_bmm150_data(0x51, &rep_xy, 1);
    uint8_t rep_z = 0x52; write_bmm150_data(0x52, &rep_z, 1);

    // 5. Set Normal Mode (Operation Mode = 0x00)
    uint8_t op_mode = 0x00; write_bmm150_data(0x4C, &op_mode, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 6. Set Read Target to BMM150_DATA0 (0x42)
    // "It is recommended to disable the auxiliary sensor interface before setting up AUX_RD_ADDR"
    write_bmi270_reg(BMI270_IF_CONF, 0x00);
    write_bmi270_reg(BMI270_AUX_READ_ADDR, BMM150_DATA0);
    write_bmi270_reg(BMI270_IF_CONF, 0x20); // Re-enable

    // 7. Transition to Data Mode (Auto-polling bursts of 8 bytes)
    write_bmi270_reg(BMI270_AUX_IF_CONFIG, 0x03);

    ESP_LOGI(TAG, "bmi270 & bmm150 initialization is done");
}

/**
 * @brief Display a 3d image
 *
 * If the object is the 3d cube, connect the projected cube points by lines and display the lines
 * For any other 3d object lit pixels on the display from provided XY coordinates
 *
 * @param projected_image: 3d matrix from Mat class after projection
 */
static void display_3d_image(dspm::Mat projected_image)
{
    // For the 3D cube, only the 6 points of the cube are transformed
    // Cube edges, connecting transformed 3D cube points are connected with lines here
    bsp_display_lock(10000);
    // ARCHITECT FIX: Render COMPASS_EDGES dynamically instead of CUBE_EDGES
    for (uint8_t i = 0; i < COMPASS_EDGES; i++) {
        points[i * 2 + 0].x = (int16_t)projected_image(compass_line_begin[i], 0);
        points[i * 2 + 0].y = (int16_t)projected_image(compass_line_begin[i], 1);
        points[i * 2 + 1].x = (int16_t)projected_image(compass_line_end[i], 0);
        points[i * 2 + 1].y = (int16_t)projected_image(compass_line_end[i], 1);
        lv_line_set_points(objs[i], &points[i * 2 + 0], 2);
        lv_obj_set_pos(objs[i], 0, 0);
    }
    bsp_display_unlock();
}

ekf_imu13states *ekf13 = NULL;

/**
 * @brief RTOS task to draw a 3d image.
 *
 * Updates 3d matrices, prepares the final 3d matrix to be displayed on the display
 *
 * @param arg: pointer to RTOS task arguments, 3d image structure in this case
 */
static void draw_3d_image_task(void *arg)
{
    image_3d_matrix_t *image = (image_3d_matrix_t *)arg;

    dspm::Mat T = dspm::Mat::eye(MATRIX_SIZE);                      // Transformation matrix
    dspm::Mat transformed_image(image->matrix_len, MATRIX_SIZE);    // 3D image matrix after transformation
    dspm::Mat projected_image(image->matrix_len, MATRIX_SIZE);      // 3D image matrix after projection

    dspm::Mat matrix_3d((float *)image->matrix[0], image->matrix_len, MATRIX_SIZE);

    float dt = 0;
    static float prev_time = 0;
    float current_time = dsp_get_cpu_cycle_count();
    float R_m[6] = {0.01, 0.01, 0.01, 0.01, 0.01, 0.01};

    static SensorCalibrator calibrator;

    while (1) {
        esp_err_t err;

        // Calculate dt for kalman filter
        current_time = dsp_get_cpu_cycle_count();
        if (current_time > prev_time) {
            dt = current_time - prev_time;
            dt = dt / 240000000.0;
        }
        prev_time = current_time;

        // Read and convert data from bmi270 and bmm150 sensors
        err = read_bmi270_data(BMI270_AUX_DATA0, (uint8_t *)sensors_data, 20);
        // ARCHITECT FIX: Stage 3 Pipeline Termination (NED Translation)
        BodyVectors body = stage1_hal_transform((int16_t*)sensors_data);
        
        // ARCHITECT FIX: Direct Hardware-to-ESKF Pipeline
        // Bypassing redundant FRD/NED abstractions. 
        // Applying mathematically proven silicon offsets to the magnetometer.
        float calib_mag[3];
        calib_mag[0] = body.mag[0] - (-143.5f);
        calib_mag[1] = body.mag[1] - (85.0f);
        calib_mag[2] = body.mag[2] - (325.0f);

        static uint32_t telemetry_counter = 0;
        if (telemetry_counter++ % 50 == 0) {
            ESP_LOGI(TAG, "--- ESKF INPUT (NATIVE XYZ) ---");
            ESP_LOGI(TAG, "ACC | X: %7.0f | Y: %7.0f | Z: %7.0f", body.accel[0], body.accel[1], body.accel[2]);
            ESP_LOGI(TAG, "GYR | X: %7.0f | Y: %7.0f | Z: %7.0f", body.gyro[0], body.gyro[1], body.gyro[2]);
            ESP_LOGI(TAG, "MAG | X: %7.0f | Y: %7.0f | Z: %7.0f", calib_mag[0], calib_mag[1], calib_mag[2]);
            ESP_LOGI(TAG, "-------------------------------");
        }

        // Feed the native, right-handed XYZ vectors directly into the ESKF Matrix Engine
        dspm::Mat gyro_input_mat(body.gyro, 3, 1);
        dspm::Mat accel_input_mat(body.accel, 3, 1);
        dspm::Mat mag_input_mat(calib_mag, 3, 1);

        accel_input_mat = accel_input_mat / 32768.0f * 16.0f;
        
        float current_norm = mag_input_mat.norm();
        if (current_norm > 0.001f) {
            mag_input_mat = (1.0f / current_norm) * mag_input_mat;
        }

        // range 2000 degree/sec fit to the int16 range
        
        gyro_input_mat *= (2000.0f * DEG_TO_RAD / 32768.0f);

        ekf13->Process(gyro_input_mat.data, dt);
        ekf13->UpdateRefMeasurementMagn(accel_input_mat.data, mag_input_mat.data, R_m);

        // Convert direction quaternion to rotation matrix
        dspm::Mat R1 = ekf::quat2rotm(ekf13->X.data).t();       
        // Convert rotation matrix to Euler angles
        dspm::Mat eul_angles = ekf::rotm2eul(R1);
        // Apply radian to degree
        eul_angles *= RAD_TO_DEG;
        // Apply rotation in all the axes to the transformation matrix
        update_rotation_matrix(T, eul_angles(0, 0), eul_angles(1, 0), eul_angles(2, 0));
        // Apply translation to the transformation matrix
        update_translation_matrix(T, true, ((float)BSP_LCD_H_RES / 2), ((float)BSP_LCD_V_RES / 2), 0);

        // matrix mul cube_matrix * transformation_matrix = transformed_cube
        transformed_image = matrix_3d * T;
        // matrix mul transformed_cube * perspective_matrix = projected_cube
        projected_image = transformed_image * perspective_matrix;

        display_3d_image(projected_image);
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

image_3d_matrix_t image;
void app_main(void)
{
    ekf13 = new ekf_imu13states();
    ekf13->Init();

    // Init all board components
    display = bsp_display_start();
    init_perspective_matrix(perspective_matrix);
    init_3d_matrix_struct(&image);
    
    // -- HARDWARE HIJACK: Map internal I2C pins to Legacy Port 1 --
    i2c_config_t i2c_conf = {};
    i2c_conf.mode = I2C_MODE_MASTER;
    i2c_conf.sda_io_num = 12; // CoreS3 Internal SDA
    i2c_conf.scl_io_num = 11; // CoreS3 Internal SCL
    i2c_conf.sda_pullup_en = true;
    i2c_conf.scl_pullup_en = true;
    i2c_conf.master.clk_speed = 400000; // 400kHz Fast Mode
    
    i2c_driver_delete(I2C_NUM_1);
    i2c_param_config(I2C_NUM_1, &i2c_conf);
    i2c_driver_install(I2C_NUM_1, i2c_conf.mode, 0, 0, 0);

    // --- BMI270 SILICON RESET ---
    uint8_t reset_cmd[2] = {0x7E, 0xB6};
    i2c_master_write_to_device(I2C_NUM_1, 0x69, reset_cmd, 2, 1000);
    vTaskDelay(pdMS_TO_TICKS(50)); // 50ms absolute boot delay to clear silicon
    
    bsp_display_lock(0);
    app_init();
    bsp_display_unlock();

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    xTaskCreate(draw_3d_image_task, "draw_3d_image", 16384, &image, 4, NULL);
    ESP_LOGI(TAG, "Showing 3D image");
}
