#include "ui_render.h"
#include "hal_imu.h"
#include "bsp/m5stack_core_s3.h"
#include "esp_log.h"
#include "cube_matrix.h"
#include "image_to_3d_matrix.h"
#include "sensor_ned.h"

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

lv_style_t style_red; lv_style_t style_blue; lv_style_t style_green; 
lv_display_t *display = NULL;
lv_obj_t **objs;
lv_point_precise_t *points;
lv_obj_t *status_indicator;

image_3d_matrix_t image;
dspm::Mat perspective_matrix(MATRIX_SIZE, MATRIX_SIZE);

// ARCHITECT FIX: Direct Hardware Feed to LVGL
static void touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data) {
    int16_t x, y;
    if (imu_hal_read_touch(&x, &y)) {
        static uint8_t throttle = 0;
        if (throttle++ % 10 == 0) {
            ESP_LOGI(TAG, "Touch Detected! Raw X:%d, Y:%d", x, y);
        }
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void calib_btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        ESP_LOGW(TAG, "UI Button Clicked: Triggering ESKF Calibration");
        eskf_trigger_calibration();
    }
}

void ui_render_init(void) {
    display = bsp_display_start();
    init_perspective_matrix(perspective_matrix);
    
    image.matrix = jet_vectors_3d;
    image.matrix_len = JET_POINTS;

    bsp_display_lock(0);

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

    status_indicator = lv_obj_create(lv_screen_active());
    lv_obj_set_size(status_indicator, 15, 15);
    lv_obj_align(status_indicator, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_radius(status_indicator, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);

    lv_obj_t * calib_btn = lv_button_create(lv_screen_active());
    lv_obj_set_size(calib_btn, 120, 40);
    lv_obj_align(calib_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(calib_btn, calib_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_label = lv_label_create(calib_btn);
    lv_label_set_text(btn_label, "Calibrate");
    lv_obj_center(btn_label);

    bsp_display_unlock();
}

void ui_render_update_3d(quaternion_t *q, bool is_deadlocked) {
    dspm::Mat T = dspm::Mat::eye(MATRIX_SIZE);
    dspm::Mat transformed_image(image.matrix_len, MATRIX_SIZE);
    dspm::Mat projected_image(image.matrix_len, MATRIX_SIZE);
    dspm::Mat matrix_3d((float *)image.matrix[0], image.matrix_len, MATRIX_SIZE);

    float q_array[4] = {q->q0, q->q1, q->q2, q->q3};
    dspm::Mat R1 = ekf::quat2rotm(q_array);       
    
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            T(r, c) = R1(r, c);
        }
    }

    transformed_image = matrix_3d * T;
    projected_image = transformed_image * perspective_matrix;

    bsp_display_lock(10000);
    for (uint8_t i = 0; i < JET_EDGES; i++) {
        points[i * 2 + 0].x =  (int16_t)projected_image(jet_line_begin[i], 0) + (BSP_LCD_H_RES / 2);
        points[i * 2 + 0].y = -(int16_t)projected_image(jet_line_begin[i], 1) + (BSP_LCD_V_RES / 2);
        points[i * 2 + 1].x =  (int16_t)projected_image(jet_line_end[i], 0) + (BSP_LCD_H_RES / 2);
        points[i * 2 + 1].y = -(int16_t)projected_image(jet_line_end[i], 1) + (BSP_LCD_V_RES / 2);
        
        lv_line_set_points(objs[i], &points[i * 2 + 0], 2);
        lv_obj_set_pos(objs[i], 0, 0);
    }

    if (is_deadlocked) {
        lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(status_indicator, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
    }
    bsp_display_unlock();
}
