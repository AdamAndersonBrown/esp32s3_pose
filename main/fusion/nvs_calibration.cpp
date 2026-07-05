#include "nvs_calibration.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "NVS_CALIBRATION";
static const char *NVS_NAMESPACE = "eskf_data";
static const char *NVS_KEY = "hard_iron";

esp_err_t eskf_load_calibration(hard_iron_profile_t *profile) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) return err;

    size_t required_size = sizeof(hard_iron_profile_t);
    err = nvs_get_blob(my_handle, NVS_KEY, profile, &required_size);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Loaded Hard-Iron Profile: X:%.1f Y:%.1f Z:%.1f", 
                 profile->offset_x, profile->offset_y, profile->offset_z);
    } else {
        ESP_LOGW(TAG, "No calibration profile found. Reverting to factory defaults.");
        profile->is_calibrated = false;
    }
    
    nvs_close(my_handle);
    return err;
}

esp_err_t eskf_save_calibration(hard_iron_profile_t *profile) {
    nvs_handle_t my_handle;
    profile->is_calibrated = true;
    
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(my_handle, NVS_KEY, profile, sizeof(hard_iron_profile_t));
    if (err == ESP_OK) {
        err = nvs_commit(my_handle);
        ESP_LOGI(TAG, "Successfully committed new Hard-Iron profile to flash.");
    }
    
    nvs_close(my_handle);
    return err;
}
