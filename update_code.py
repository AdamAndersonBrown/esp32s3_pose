import os

filepath = os.path.join("main", "core", "app_main.cpp")

with open(filepath, "r") as f:
    content = f.read()

# 1. Revert to original execution order for Auxiliary Writes (Data MUST precede Address)
old_bmm_write = """esp_err_t write_bmm150_data(uint8_t addr, uint8_t *data, int length)
{
    // ARCHITECT FIX: Set target address FIRST (0x4E)
    i2c_write_buffer[0] = BMI270_AUX_WRITE_ADDR;
    i2c_write_buffer[1] = addr;
    esp_err_t err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 2, 1000);

    // ARCHITECT FIX: Push data payload SECOND (0x4F) to trigger the hardware transaction
    i2c_write_buffer[0] = BMI270_AUX_WRITE_DATA;
    i2c_write_buffer[1] = data[0];
    err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 2, 1000);

    // ARCHITECT FIX: MANDATORY hardware delay. 
    // The BMI270 requires time to process the auxiliary I2C write across the physical traces before accepting the next instruction.
    vTaskDelay(pdMS_TO_TICKS(5));

    return err;
}"""

new_bmm_write = """esp_err_t write_bmm150_data(uint8_t addr, uint8_t *data, int length)
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
}"""

if old_bmm_write in content:
    content = content.replace(old_bmm_write, new_bmm_write)
else:
    print("Warning: write_bmm150_data block not found for replacement.")


# 2. Re-sequence the Setup Mode to occur while the auxiliary interface is explicitly ENABLED
old_setup = """    i2c_write_buffer[0] = BMI270_IF_CONF;
    i2c_write_buffer[1] = 0x00;
    err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 2, 1000);

    // --- ARCHITECT FIX: BMM150 SETUP (AFTER MICROCODE LOAD) ---
    // 1. Set Target I2C Device Address (0x10 << 1 = 0x20)
    write_bmi270_reg(BMI270_AUX_DEV_ID, 0x20);

    // 2. Enter Setup Mode (manual auxiliary routing)
    write_bmi270_reg(BMI270_AUX_IF_CONFIG, 0x80);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 3. Wake BMM150 from Suspend to Sleep (Power Control = 1)
    uint8_t pwr_ctrl = 0x01;
    write_bmm150_data(BMM150_REG_POWER_CONTROL, &pwr_ctrl, 1);
    vTaskDelay(pdMS_TO_TICKS(15)); // Strict delay for oscillators

    // 4. Configure High-Accuracy Repetitions (XY: 47 -> 0x17, Z: 83 -> 0x52)
    uint8_t rep_xy = 0x17; write_bmm150_data(0x51, &rep_xy, 1);
    uint8_t rep_z = 0x52; write_bmm150_data(0x52, &rep_z, 1);

    // 5. Set Normal Mode (Operation Mode = 0x00)
    uint8_t op_mode = 0x00; write_bmm150_data(0x4c, &op_mode, 1);
    vTaskDelay(pdMS_TO_TICKS(15));

    // 6. Set Read Target to BMM150_DATA0 (0x42)
    write_bmi270_reg(BMI270_AUX_READ_ADDR, BMM150_DATA0);

    i2c_write_buffer[0] = BMI270_IF_CONF;
    i2c_write_buffer[1] = 0x20;
    err = i2c_master_write_to_device(I2C_NUM_1, 0x69, i2c_write_buffer, 2, 1000);

    // 7. Transition to Data Mode (Auto-polling bursts)
    write_bmi270_reg(BMI270_AUX_IF_CONFIG, 0x03);

    ESP_LOGI(TAG, "bmi270 & bmm150 initialization is done");"""

new_setup = """    // --- ARCHITECT FIX: BMM150 SETUP (AFTER MICROCODE LOAD) ---
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

    ESP_LOGI(TAG, "bmi270 & bmm150 initialization is done");"""

if old_setup in content:
    content = content.replace(old_setup, new_setup)
else:
    print("Warning: Initialization setup block not found for replacement.")

with open(filepath, "w") as f:
    f.write(content)

print(f"Successfully patched {filepath}")