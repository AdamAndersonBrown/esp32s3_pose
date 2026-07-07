#include "calibration.h"
#include "esp_log.h"

static const char *TAG = "CALIBRATION";
static bool is_calibrating = false;
static uint16_t calib_samples = 0;
static float mag_min[3] = {99999.0f, 99999.0f, 99999.0f};
static float mag_max[3] = {-99999.0f, -99999.0f, -99999.0f};
static hard_iron_profile_t mag_profile;

void calibration_init(void) {
    if (eskf_load_calibration(&mag_profile) != ESP_OK || !mag_profile.is_calibrated) {
        mag_profile.offset_x = -143.5f;
        mag_profile.offset_y = 85.0f;
        mag_profile.offset_z = 325.0f;
        mag_profile.is_calibrated = true; 
        eskf_save_calibration(&mag_profile);
    }
}

void calibration_trigger(void) {
    if (!is_calibrating) {
        ESP_LOGW(TAG, "=== CALIBRATION MODE TRIGGERED ===");
        mag_min[0] = 99999.0f; mag_min[1] = 99999.0f; mag_min[2] = 99999.0f;
        mag_max[0] = -99999.0f; mag_max[1] = -99999.0f; mag_max[2] = -99999.0f;
        calib_samples = 0;
        is_calibrating = true;
    }
}

bool calibration_is_active(void) {
    return is_calibrating;
}

bool calibration_process_sample(float mx, float my, float mz) {
    if (mx < mag_min[0]) mag_min[0] = mx;
    if (mx > mag_max[0]) mag_max[0] = mx;
    if (my < mag_min[1]) mag_min[1] = my;
    if (my > mag_max[1]) mag_max[1] = my;
    if (mz < mag_min[2]) mag_min[2] = mz;
    if (mz > mag_max[2]) mag_max[2] = mz;
    
    calib_samples++;
    if (calib_samples >= 900) {
        mag_profile.offset_x = (mag_max[0] + mag_min[0]) / 2.0f;
        mag_profile.offset_y = (mag_max[1] + mag_min[1]) / 2.0f;
        mag_profile.offset_z = (mag_max[2] + mag_min[2]) / 2.0f;
        mag_profile.is_calibrated = true;
        eskf_save_calibration(&mag_profile);
        is_calibrating = false;
        return true; 
    }
    return false;
}

void calibration_apply(float* hx, float* hy, float* hz) {
    if (mag_profile.is_calibrated) {
        *hx -= mag_profile.offset_x;
        *hy -= mag_profile.offset_y;
        *hz -= mag_profile.offset_z;
    }
}
