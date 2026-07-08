#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_render.h"
#include "hal_imu.h"
#include "hal_touch.h"
#include "bsp/m5stack_core_s3.h"
#include "esp_log.h"
#include "cube_matrix.h"

#include "eskf_fusion.h"
#include <stdio.h>

static const char *TAG = "UI_RENDER";

#define M5_CUBE_SIDE (BSP_LCD_V_RES / 4)
#define JET_POINTS 6
#define JET_EDGES 11

const float jet_vectors_3d[JET_POINTS][MATRIX_SIZE] = {
    {  80,   0,   0, 1}, {-40,   60,   0, 1}, {-40,  -60,   0, 1}, 
    {-60,    0,   0, 1}, {-70,    0, -40, 1}, {-20,    0,  20, 1}  
};

const uint8_t jet_line_begin[JET_EDGES] = {0, 0, 1, 2, 3, 1, 2, 0, 1, 2, 3};
const uint8_t jet_line_end[JET_EDGES]   = {1, 2, 3, 3, 4, 4, 4, 5, 5, 5, 5};

static lv_style_t style_red; static lv_style_t style_blue; static lv_style_t style_green; 
static lv_display_t *display = NULL;
static lv_obj_t **objs;
static lv_point_precise_t *points;
static lv_obj_t *status_indicator;
static lv_obj_t *vel_label;
static lv_obj_t *pos_label;
static lv_obj_t * pmic_label;
static lv_obj_t * temp_label;

// ARCHITECT FIX: Calibration Overlay Objects
static lv_obj_t * calib_overlay;
static lv_obj_t * calib_overlay_label;

static image_3d_matrix_t image;
static dspm::Mat perspective_matrix(MATRIX_SIZE, MATRIX_SIZE);

// ARCHITECT FIX: Forward declare timer callback for C++ top-to-bottom compilation
static void ui_render_timer_cb(lv_timer_t * timer);

// ==========================================
// RESTORED: Bare-Metal Hardware Feed to LVGL
// ==========================================
static void touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data) {
    int16_t x, y;
    if (touch_hal_read(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ==========================================
// RESTORED: 5-Second Hold Timer
// ==========================================
static uint32_t press_start_time = 0;
static bool press_triggered = false;

static void calib_btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t * label = lv_obj_get_child(btn, 0);
    
    if (code == LV_EVENT_PRESSED) {
        press_start_time = lv_tick_get();
        press_triggered = false;
    } 
    else if (code == LV_EVENT_PRESSING) {
        if (!press_triggered) {
            uint32_t elapsed = lv_tick_get() - press_start_time;
            if (elapsed > 5000) {
                press_triggered = true;
                ESP_LOGW(TAG, "5s Hold Reached: Triggering ESKF Calibration");
                eskf_trigger_calibration();
                lv_label_set_text(label, "Calibrate");
            } else {
                char buf[32];
                snprintf(buf, sizeof(buf), "Hold %lu...", 5 - (elapsed / 1000));
                lv_label_set_text(label, buf);
            }
        }
    } 
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        press_start_time = 0;
        press_triggered = false;
        lv_label_set_text(label, "Calibrate");
    }
}

void ui_render_init(void) {

    vTaskDelay(pdMS_TO_TICKS(300)); // Let LVGL boot safely

    display = bsp_display_start();
    init_perspective_matrix(perspective_matrix);
    
    
    image.matrix = jet_vectors_3d;
    image.matrix_len = JET_POINTS;

    while(lv_scr_act() == NULL) { vTaskDelay(10); }
    while(!lvgl_port_lock(100)) { vTaskDelay(pdMS_TO_TICKS(10)); }

    // ARCHITECT FIX: Safely apply background color inside the secured mutex
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x222222), 0);

    // ARCHITECT FIX: Bind Bare-Metal Touch to LVGL
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    lv_style_init(&style_red); lv_style_init(&style_blue); lv_style_init(&style_green);

    lv_style_set_line_color(&style_red, lv_palette_main(LV_PALETTE_RED));
    lv_style_set_line_width(&style_red, 10);
    lv_style_set_line_color(&style_blue, lv_palette_main(LV_PALETTE_BLUE));
    lv_style_set_line_width(&style_blue, 10);
    lv_style_set_line_color(&style_green, lv_palette_main(LV_PALETTE_GREEN));
    lv_style_set_line_width(&style_green, 12);
    lv_style_set_line_rounded(&style_green, true);

    objs = (lv_obj_t **)malloc(JET_EDGES * sizeof(lv_obj_t *));
    points = (lv_point_precise_t *)malloc(JET_EDGES * 2 * sizeof(lv_point_precise_t));

    for (uint8_t i = 0; i < JET_EDGES; i++) {
        objs[i] = lv_line_create(lv_screen_active());
        if (i >= 7) lv_obj_add_style(objs[i], &style_green, 0);
        else if (i >= 4) lv_obj_add_style(objs[i], &style_blue, 0);
        else lv_obj_add_style(objs[i], &style_red, 0);
    }

    
    vel_label = lv_label_create(lv_screen_active());
    lv_obj_align(vel_label, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_text_color(vel_label, lv_color_hex(0xFFFF00), 0); // Yellow
    lv_obj_set_style_bg_color(vel_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(vel_label, LV_OPA_50, 0);                  // 50% transparent
    lv_label_set_text(vel_label, "V(cm/s):     0     0     0");

    pos_label = lv_label_create(lv_screen_active());
    lv_obj_align(pos_label, LV_ALIGN_TOP_LEFT, 10, 40);                // Spaced down by 30 pixels
    lv_obj_set_style_text_color(pos_label, lv_color_hex(0x00FFFF), 0); // Cyan
    lv_obj_set_style_bg_color(pos_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(pos_label, LV_OPA_50, 0);                  // 50% transparent
    lv_label_set_text(pos_label, "P(cm):     0     0     0");

    status_indicator = lv_obj_create(lv_screen_active());
    lv_obj_set_size(status_indicator, 15, 15);
    lv_obj_align(status_indicator, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_radius(status_indicator, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);

    lv_obj_t * calib_btn = lv_button_create(lv_screen_active());
    lv_obj_set_size(calib_btn, 120, 40);
    lv_obj_align(calib_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(calib_btn, calib_btn_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t * btn_label = lv_label_create(calib_btn);
    lv_label_set_text(btn_label, "Calibrate");
    lv_obj_center(btn_label);

    // ARCHITECT FIX: Create Full-Screen Overlay
    calib_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(calib_overlay, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_style_bg_color(calib_overlay, lv_palette_main(LV_PALETTE_BLUE_GREY), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(calib_overlay, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(calib_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(calib_overlay, 0, LV_PART_MAIN);
    lv_obj_align(calib_overlay, LV_ALIGN_CENTER, 0, 0);

    calib_overlay_label = lv_label_create(calib_overlay);
    lv_label_set_text(calib_overlay_label, "CALIBRATION MODE\n\nRotate device in a\n3D Figure-8...");
    lv_obj_set_style_text_align(calib_overlay_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(calib_overlay_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(calib_overlay_label);

    // Hide it by default
    lv_obj_add_flag(calib_overlay, LV_OBJ_FLAG_HIDDEN);

    
    // Ping physics state safely INSIDE the mutex
    
    // CRITICAL: Start the 20Hz rendering loop!
    pmic_label = lv_label_create(lv_scr_act());
    
    // ARCHITECT FIX: Create Thermal Telemetry Label
    temp_label = lv_label_create(lv_scr_act());
    lv_label_set_text(temp_label, "SYS: --.- C");
    lv_obj_align(temp_label, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_text_color(temp_label, lv_color_hex(0xFF8800), 0); // Orange warning color

    lv_label_set_text(pmic_label, "PWR: -- mA");
    lv_obj_align(pmic_label, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_text_color(pmic_label, lv_color_hex(0xFFFFFF), 0);

    lv_timer_create(ui_render_timer_cb, 50, NULL);
    lvgl_port_unlock();
    
    }

// ARCHITECT FIX: Native LVGL Timer Callback (runs safely in UI thread)
static void ui_render_timer_cb(lv_timer_t * timer) {
    if (pmic_label == NULL) return;

    eskf_state_t state;
    eskf_get_latest_state(&state);
    
    quaternion_t *q = &state.q;
    if (global_state.is_charging) { // ARCHITECT FIX: Bypass broken struct copy and read global directly
        lv_label_set_text_fmt(pmic_label, "BAT: %d%% %s", state.pmic_percentage, LV_SYMBOL_CHARGE);
        lv_obj_set_style_text_color(pmic_label, lv_color_hex(0x00FF00), 0); // Green when charging
    } else {
        lv_label_set_text_fmt(pmic_label, "BAT: %d%%", state.pmic_percentage);
        lv_obj_set_style_text_color(pmic_label, lv_color_hex(0xFFFFFF), 0); // White when on battery
    }
    bool is_deadlocked = state.is_deadlocked;
    float *vel = state.vel;
    float *pos = state.pos;
    (void)pos; // Suppress unused variable warning
    if(temp_label != NULL) {
        // ARCHITECT FIX: Bypass newlib-nano float formatting trap
        int temp_whole = (int)global_state.system_temp;
        int temp_frac = (int)((global_state.system_temp - temp_whole) * 10.0f);
        if (temp_frac < 0) temp_frac = -temp_frac; 
        lv_label_set_text_fmt(temp_label, "SYS: %d.%d C", temp_whole, temp_frac);
    }
    float *pure_pos = state.pure_pos;
    bool is_moving = state.is_moving;

    static bool was_moving = false;
    if (is_moving != was_moving) {
        if (is_moving) {
            lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x660000), 0); // Dark Red
        } else {
            lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x222222), 0); // Dark Grey
        }
        lv_timer_set_period(timer, is_moving ? 33 : 200); // 30Hz active, 5Hz idle
        was_moving = is_moving;
    }
    dspm::Mat T = dspm::Mat::eye(MATRIX_SIZE);
    dspm::Mat transformed_image(image.matrix_len, MATRIX_SIZE);
    dspm::Mat projected_image(image.matrix_len, MATRIX_SIZE);
    dspm::Mat matrix_3d((float *)image.matrix[0], image.matrix_len, MATRIX_SIZE);

    if (!state.is_sleeping) {
        float q_array[4] = {q->q0, q->q1, q->q2, q->q3};
    dspm::Mat R1 = ekf::quat2rotm(q_array);       
    
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            T(r, c) = R1(r, c);
        }
    }

    transformed_image = matrix_3d * T;
    projected_image = transformed_image * perspective_matrix;
    }

    while(lv_scr_act() == NULL) { vTaskDelay(10); }
    while(!lvgl_port_lock(100)) { vTaskDelay(pdMS_TO_TICKS(10)); }

    // ARCHITECT FIX: Safely apply background color inside the secured mutex
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x222222), 0);
    
    // ARCHITECT FIX: UI Overlay State Polling
    static bool was_calibrating = false;
    bool is_calib = eskf_is_calibrating();
    
    if (is_calib != was_calibrating) {
        if (is_calib) {
            lv_obj_clear_flag(calib_overlay, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(calib_overlay);
        } else {
            lv_obj_add_flag(calib_overlay, LV_OBJ_FLAG_HIDDEN);
        }
        was_calibrating = is_calib;
    }

    if (!state.is_sleeping) {
        for (uint8_t i = 0; i < JET_EDGES; i++) {
        points[i * 2 + 0].x =  (int16_t)projected_image(jet_line_begin[i], 0) + (BSP_LCD_H_RES / 2);
        points[i * 2 + 0].y = -(int16_t)projected_image(jet_line_begin[i], 1) + (BSP_LCD_V_RES / 2);
        points[i * 2 + 1].x =  (int16_t)projected_image(jet_line_end[i], 0) + (BSP_LCD_H_RES / 2);
        points[i * 2 + 1].y = -(int16_t)projected_image(jet_line_end[i], 1) + (BSP_LCD_V_RES / 2);
        
        lv_line_set_points(objs[i], &points[i * 2 + 0], 2);
        lv_obj_set_pos(objs[i], 0, 0);
    }
    }

    if (is_deadlocked) {
        lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
    }

    // ARCHITECT FIX: Inject Kinematics into LVGL Labels (Integer Cast for OS Safety)
    lv_label_set_text_fmt(vel_label, "V(cm/s): %5d %5d %5d", 
                          (int)(vel[0] * 100.0f), (int)(vel[1] * 100.0f), (int)(vel[2] * 100.0f));
    // ARCHITECT FIX: Bind UI distance label to the frictionless Odometry engine
    lv_label_set_text_fmt(pos_label, "P(cm): %5d %5d %5d", 
                          (int)(pure_pos[0] * 100.0f), (int)(pure_pos[1] * 100.0f), (int)(pure_pos[2] * 100.0f));

    
    // Ping physics state safely INSIDE the mutex
    lvgl_port_unlock();
}
