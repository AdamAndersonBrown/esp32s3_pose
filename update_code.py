import os
import re

app_main_path = os.path.join("main", "core", "app_main.cpp")

with open(app_main_path, "r") as f:
    content = f.read()

# 1. Inject the global LVGL status indicator object
pattern_global = re.compile(r"(lv_obj_t \*\*objs;\s*lv_point_precise_t \*points;)")
replacement_global = r"""\1
lv_obj_t *status_indicator; // ARCHITECT FIX: Watchdog UI Element"""
content, count1 = pattern_global.subn(replacement_global, content)

# 2. Initialize the status indicator during app_init
pattern_init = re.compile(r"(\s+)(esp_err_t err = ESP_OK;)")
replacement_init = r"""\1// Initialize Watchdog UI Indicator
    status_indicator = lv_obj_create(lv_screen_active());
    lv_obj_set_size(status_indicator, 15, 15);
    lv_obj_align(status_indicator, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_radius(status_indicator, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);

    \2"""
content, count2 = pattern_init.subn(replacement_init, content)

# 3. Inject the state variables into the graphics task
pattern_task_vars = re.compile(r"(static SensorCalibrator calibrator;\s*)(while \(1\) \{)")
replacement_task_vars = r"""\1static float prev_mag[3] = {0, 0, 0};
    static uint8_t deadlock_counter = 0;
    bool sensor_deadlock = false;

    \2"""
content, count3 = pattern_task_vars.subn(replacement_task_vars, content)

# 4. Inject the Watchdog logic immediately after reading the magnetometer
pattern_logic = re.compile(r"(calib_mag\[2\] = body\.mag\[2\] - \(325\.0f\);)(\s+static uint32_t telemetry_counter = 0;)")
replacement_logic = r"""\1

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
        prev_mag[2] = calib_mag[2];\2"""
content, count4 = pattern_logic.subn(replacement_logic, content)

# 5. Push the Watchdog status to the LVGL display
pattern_display = re.compile(r"(display_3d_image\(projected_image\);)(\s+vTaskDelay)")
replacement_display = r"""\1

        // Update Watchdog Visual Telemetry
        bsp_display_lock(10000);
        if (sensor_deadlock) {
            lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
        }
        bsp_display_unlock();\2"""
content, count5 = pattern_display.subn(replacement_display, content)

if all(c > 0 for c in [count1, count2, count3, count4, count5]):
    with open(app_main_path, "w") as f:
        f.write(content)
    print("Successfully injected the Hardware Variance Watchdog and LVGL telemetry indicator.")
else:
    print(f"Error finding blocks. C1:{count1} C2:{count2} C3:{count3} C4:{count4} C5:{count5}")