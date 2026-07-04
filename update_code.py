import os

# ARCHITECT FIX: Target the correct subdirectory based on the project structure
filepath = os.path.join("main", "core", "app_main.cpp")

with open(filepath, "r") as f:
    content = f.read()

# 1. Fix write_bmm150_data execution order
old_bmm_write = """esp_err_t write_bmm150_data(uint8_t addr, uint8_t *data, int length)
{
    i2c_write_buffer[0] = BMI270_AUX_WRITE_DATA;
    i2c_write_buffer[1] = data[0];
    esp_err_t err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 2, 1000);

    i2c_write_buffer[0] = BMI270_AUX_WRITE_ADDR;
    i2c_write_buffer[1] = addr;
    err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 2, 1000);

    return err;
}"""

new_bmm_write = """esp_err_t write_bmm150_data(uint8_t addr, uint8_t *data, int length)
{
    // ARCHITECT FIX: Set target address FIRST (0x4E)
    i2c_write_buffer[0] = BMI270_AUX_WRITE_ADDR;
    i2c_write_buffer[1] = addr;
    esp_err_t err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 2, 1000);

    // ARCHITECT FIX: Push data payload SECOND (0x4F) to trigger the hardware transaction
    i2c_write_buffer[0] = BMI270_AUX_WRITE_DATA;
    i2c_write_buffer[1] = data[0];
    err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 2, 1000);

    return err;
}"""
content = content.replace(old_bmm_write, new_bmm_write)

# 2. Fix write_bmi270_data buffer addressing
old_bmi_write = """    if (length < 32) {
        i2c_write_buffer[0] = BMI270_AUX_WRITE_DATA;"""
new_bmi_write = """    if (length < 32) {
        i2c_write_buffer[0] = addr; // ARCHITECT FIX: Route to intended register"""
content = content.replace(old_bmi_write, new_bmi_write)

# 3. Consolidate Setup in app_init()
old_app_init_setup = """    i2c_write_buffer[0] = BMI270_AUX_IF_CONFIG;
    i2c_write_buffer[1] = 0x80;
    err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 2, 1000);

    i2c_write_buffer[0] = BMI270_AUX_IF_CONFIG;
    err = i2c_master_write_read_device(I2C_NUM_1, 0x69, i2c_write_buffer, 1, i2c_read_buffer, 1, 1000);
    // vTaskDelay(1);

    err = read_bmm150_data(BMM150_REG_POWER_CONTROL, i2c_read_buffer, 1);
    i2c_read_buffer[0] = 1;
    write_bmm150_data(BMM150_REG_POWER_CONTROL, i2c_read_buffer, 1);
    read_bmm150_data(BMM150_REG_POWER_CONTROL, i2c_read_buffer, 1);
    vTaskDelay(1);

    err = read_bmm150_data(BMM150_SHIP_ID, i2c_read_buffer, 1);
    ESP_LOGI(TAG, "bmm150 chip ID = 0x%2.2x (should be 0x32), err = %2.2x", i2c_read_buffer[0], err);

    err = read_bmm150_data(0x4c, i2c_read_buffer, 1);
    i2c_read_buffer[0] = 0x3 << 3;
    write_bmm150_data(0x4c, i2c_read_buffer, 1);
    err = read_bmm150_data(0x4c, i2c_read_buffer, 1);
    vTaskDelay(1);"""

new_app_init_setup = """    // ARCHITECT FIX: Set Target I2C Device Address (0x10 << 1 = 0x20)
    write_bmi270_reg(BMI270_AUX_DEV_ID, 0x20);

    // Enter Setup Mode
    write_bmi270_reg(BMI270_AUX_IF_CONFIG, 0x80);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Wake BMM150 from Suspend to Sleep (Power Control = 1)
    uint8_t pwr_ctrl = 0x01;
    write_bmm150_data(BMM150_REG_POWER_CONTROL, &pwr_ctrl, 1);
    vTaskDelay(pdMS_TO_TICKS(15)); // Strict delay for oscillators

    // Configure High-Accuracy Repetitions (XY: 47 -> 0x17, Z: 83 -> 0x52)
    uint8_t rep_xy = 0x17;
    write_bmm150_data(0x51, &rep_xy, 1);
    uint8_t rep_z = 0x52;
    write_bmm150_data(0x52, &rep_z, 1);

    // Set Normal Mode (Operation Mode = 0x00)
    uint8_t op_mode = 0x00;
    write_bmm150_data(0x4c, &op_mode, 1);
    vTaskDelay(pdMS_TO_TICKS(15));"""
content = content.replace(old_app_init_setup, new_app_init_setup)

# 4. Remove inline thread setup
old_task_inline = """        // --- NATIVE BMM150 WAKE UP ---
        {
            uint8_t pwr = 0x01; write_bmm150_data(0x4B, &pwr, 1); vTaskDelay(pdMS_TO_TICKS(15));
            uint8_t repXY = 0x04; write_bmm150_data(0x51, &repXY, 1);
            uint8_t repZ = 0x0F; write_bmm150_data(0x52, &repZ, 1);
            uint8_t op = 0x00; write_bmm150_data(0x4C, &op, 1); vTaskDelay(pdMS_TO_TICKS(15));
        }
        // -----------------------------"""
new_task_inline = """        // ARCHITECT FIX: BMM150 Initialization moved safely to app_init()"""
content = content.replace(old_task_inline, new_task_inline)

with open(filepath, "w") as f:
    f.write(content)

print(f"Successfully patched {filepath}")