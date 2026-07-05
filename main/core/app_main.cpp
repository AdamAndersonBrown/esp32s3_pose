#include "nvs_flash.h"
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
#define JET_POINTS 6
#define JET_EDGES 11

const float jet_vectors_3d[JET_POINTS][MATRIX_SIZE] = {
    // Defined in the True Earth Frame (North=X, East=Y, Down=Z)
    {  80,   0,   0, 1}, // 0: Nose (North, +X)
    {-40,   60,   0, 1}, // 1: Right Wing (East, +Y)
    {-40,  -60,   0, 1}, // 2: Left Wing (West, -Y)
    {-60,    0,   0, 1}, // 3: Tail Base (South, -X)
    {-70,    0, -40, 1}, // 4: Tail Fin (Up, -Z)
    {-20,    0,  20, 1}  // 5: Belly (Down, +Z)
};

const uint8_t jet_line_begin[JET_EDGES] = {0, 0, 1, 2, 3, 1, 2, 0, 1, 2, 3};
const uint8_t jet_line_end[JET_EDGES]   = {1, 2, 3, 3, 4, 4, 4, 5, 5, 5, 5};

static void init_3d_matrix_struct(image_3d_matrix_t *image)
{
    image->matrix = jet_vectors_3d;
    image->matrix_len = JET_POINTS;
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
lv_obj_t *status_indicator; // ARCHITECT FIX: Watchdog UI Element


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

    objs = (lv_obj_t **)malloc(JET_EDGES * sizeof(lv_obj_t *));
    points = (lv_point_precise_t *)malloc(JET_EDGES * 2 * sizeof(lv_point_precise_t));

    for (uint8_t i = 0; i < JET_EDGES; i++) {
        objs[i] = lv_line_create(lv_screen_active());
        // Color code: Wings/Fuselage = Red, Tail Fin = Blue, Belly = Green
        if (i >= 7) lv_obj_add_style(objs[i], &style_green, 0);
        else if (i >= 4) lv_obj_add_style(objs[i], &style_blue, 0);
        else lv_obj_add_style(objs[i], &style_red, 0);
    }

    // Init the bmi270 and bmm150 chips
    // Initialize Watchdog UI Indicator
    status_indicator = lv_obj_create(lv_screen_active());
    lv_obj_set_size(status_indicator, 15, 15);
    lv_obj_align(status_indicator, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_radius(status_indicator, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);

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
    for (uint8_t i = 0; i < JET_EDGES; i++) {
        // ARCHITECT FIX: Cartesian to Screen Mapping
        // LVGL Y-axis is inverted (Down is positive). We center the image AFTER projection.
        points[i * 2 + 0].x =  (int16_t)projected_image(jet_line_begin[i], 0) + (BSP_LCD_H_RES / 2);
        points[i * 2 + 0].y = -(int16_t)projected_image(jet_line_begin[i], 1) + (BSP_LCD_V_RES / 2);
        points[i * 2 + 1].x =  (int16_t)projected_image(jet_line_end[i], 0) + (BSP_LCD_H_RES / 2);
        points[i * 2 + 1].y = -(int16_t)projected_image(jet_line_end[i], 1) + (BSP_LCD_V_RES / 2);
        
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
    // ARCHITECT FIX: Covariance Tuning
        // Accel (0-2): 0.1 (Trusted for gravity)
        // Mag (3-5): 5.0 (High variance, acts as a slow leash for gyro drift)
        // ARCHITECT FIX: Geometric Covariance Tuning
        // Vectors are normalized to 1.0, so R_m represents radians squared.
        // Accel: ~2 degrees expected noise -> 0.001
        // Mag:  ~10 degrees expected environmental distortion -> 0.03
        // ARCHITECT FIX: Kinetic Acceleration Tolerance
        // Raised Accel covariance to 0.5 to reject centripetal hand movement.
        // Gyro now handles dynamic 3D tilt; Accel handles long-term gravity leveling.
        float R_m[6] = {0.5f, 0.5f, 0.5f, 0.03f, 0.03f, 0.03f};

    static SensorCalibrator calibrator;

    static float prev_mag[3] = {0, 0, 0};
    static uint8_t deadlock_counter = 0;
    bool sensor_deadlock = false;

    while (1) {
        esp_err_t err;

        // Calculate dt for kalman filter
        current_time = dsp_get_cpu_cycle_count();
        if (current_time > prev_time) {
            dt = current_time - prev_time;
            dt = dt / 160000000.0; // ARCHITECT FIX: CoreS3 runs at 160MHz
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

        // ARCHITECT FIX: Hardware Variance Watchdog
        // Checks if the auxiliary I2C bus has frozen by monitoring identical consecutive readings.
        if (calib_mag[0] == prev_mag[0] && calib_mag[1] == prev_mag[1] && calib_mag[2] == prev_mag[2]) {
            if (deadlock_counter < 255) deadlock_counter++;
            if (deadlock_counter > 50) sensor_deadlock = true;
        } else {
            deadlock_counter = 0;
            sensor_deadlock = false;
        }
        prev_mag[0] = calib_mag[0];
        prev_mag[1] = calib_mag[1];
        prev_mag[2] = calib_mag[2];

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

        // Convert direction quaternion to rotation matrix (Body to Earth)
        // Convert direction quaternion to Body-to-Earth matrix, then transpose to Earth-to-Body
        // Get Body-to-Earth matrix. 
        // Because matrix_3d * T uses row-vector right-multiplication, 
        // the math engine inherently transposes it to Earth-to-Body for us.
        dspm::Mat R1 = ekf::quat2rotm(ekf13->X.data);       
        
        // ARCHITECT FIX: World-Lock the 3D Object
        // To lock the object to the real world, we rotate it by Earth-to-Body (R1^T).
        // Since transformed_image = matrix_3d * T uses row-vectors, right-multiplying by R1 
        // mathematically applies R1^T to the geometry. 
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                T(r, c) = R1(r, c);
            }
        }
        // Removed pre-projection screen translation to prevent perspective wobble.

        // matrix mul cube_matrix * transformation_matrix = transformed_cube
        transformed_image = matrix_3d * T;

        // ARCHITECT DIAGNOSTIC: Filter Divergence Check
        // Compare the Raw Magnetometer Heading against the EKF's Internal Belief
        static uint8_t diag_tick = 0;
        if (++diag_tick % 50 == 0) {
            float raw_yaw = atan2(calib_mag[1], calib_mag[0]) * (180.0f / M_PI);
            
            float q0 = ekf13->X.data[0];
            float q1 = ekf13->X.data[1];
            float q2 = ekf13->X.data[2];
            float q3 = ekf13->X.data[3];
            // Standard aerospace Z-Y-X yaw extraction from quaternion
            float ekf_yaw = atan2(2.0f * (q0 * q3 + q1 * q2), 1.0f - 2.0f * (q2 * q2 + q3 * q3)) * (180.0f / M_PI);
            
            ESP_LOGW(TAG, "DIAGNOSTIC -> Raw Mag Yaw: %0.1f deg | EKF Yaw: %0.1f deg", raw_yaw, ekf_yaw);

        // ARCHITECT DIAGNOSTIC: Magnetic Spherical Validity Monitor
        // The magnitude (radius) of the magnetic vector should remain perfectly constant.
        // If the radius expands or contracts when the device is tilted, the Z-axis 
        // calibration is failing and warping the EKF virtual horizon.
        
        static float baseline_radius = 0.0f;
        static uint32_t monitor_tick = 0;
        
        // Calculate the current 3D radius (Pythagorean theorem)
        float current_radius = sqrt((calib_mag[0] * calib_mag[0]) + 
                                    (calib_mag[1] * calib_mag[1]) + 
                                    (calib_mag[2] * calib_mag[2]));
                                    
        // Latch the baseline radius when sitting flat after boot
        if (baseline_radius == 0.0f && current_radius > 10.0f && monitor_tick > 100) {
            baseline_radius = current_radius;
            ESP_LOGI(TAG, "MAGNETIC MONITOR: Baseline Radius Locked at %.1f", baseline_radius);
        }
        monitor_tick++;

        if (baseline_radius > 0.0f && monitor_tick % 25 == 0) {
            float deviation = (fabs(current_radius - baseline_radius) / baseline_radius) * 100.0f;
            
            if (deviation > 10.0f) {
                ESP_LOGE(TAG, "CALIBRATION INVALID: Sphere Warped! Radius: %.1f (Deviation: %.1f%%)", current_radius, deviation);
            } else if (monitor_tick % 100 == 0) {
                // Print a heartbeat every few seconds to show it is tracking cleanly
                ESP_LOGI(TAG, "Magnetic Radius Stable: %.1f (Deviation: %.1f%%)", current_radius, deviation);
            }
        }
        }
        // matrix mul transformed_cube * perspective_matrix = projected_cube
        projected_image = transformed_image * perspective_matrix;

        display_3d_image(projected_image);

        // Update Watchdog Visual Telemetry
        bsp_display_lock(10000);
        if (sensor_deadlock) {
            lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
        }
        bsp_display_unlock();
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

image_3d_matrix_t image;
void app_main(void)
{
    // ARCHITECT FIX: Initialize Non-Volatile Storage (NVS)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

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
