import os
import re

fusion_dir = os.path.join("main", "fusion")
nvs_h = os.path.join(fusion_dir, "nvs_calibration.h")
nvs_cpp = os.path.join(fusion_dir, "nvs_calibration.cpp")

# 1. Generate NVS Calibration Contract
nvs_h_content = """#pragma once
#include "esp_err.h"

// STRICT DATA STRUCTURE:
// The stored magnetic footprint of the specific hardware chassis.
typedef struct {
    float offset_x;
    float offset_y;
    float offset_z;
    bool is_calibrated;
} hard_iron_profile_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Retrieves the saved Hard-Iron profile from Non-Volatile Storage.
 * @param profile Pointer to the profile struct to populate.
 * @return ESP_OK if found, ESP_ERR_NOT_FOUND if factory fresh.
 */
esp_err_t eskf_load_calibration(hard_iron_profile_t *profile);

/**
 * @brief Commits a new Hard-Iron profile to flash memory.
 * @param profile Pointer to the newly calculated profile.
 */
esp_err_t eskf_save_calibration(hard_iron_profile_t *profile);

#ifdef __cplusplus
}
#endif
"""

# 2. Generate NVS Implementation
nvs_cpp_content = """#include "nvs_calibration.h"
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
"""

with open(nvs_h, "w") as f: f.write(nvs_h_content)
with open(nvs_cpp, "w") as f: f.write(nvs_cpp_content)

# 3. Wire into CMakeLists.txt
cmake_path = os.path.join("main", "CMakeLists.txt")
if os.path.exists(cmake_path):
    with open(cmake_path, "r") as f:
        cmake_data = f.read()

    if "nvs_calibration.cpp" not in cmake_data:
        cmake_data = cmake_data.replace('\"fusion/eskf_fusion.cpp\"', '\"fusion/eskf_fusion.cpp\"\n                    \"fusion/nvs_calibration.cpp\"')
        with open(cmake_path, "w") as f:
            f.write(cmake_data)
        print("Linked NVS module into CMakeLists.txt")

# 4. Inject NVS Bootloader sequence into app_main.cpp
app_main_path = os.path.join("main", "core", "app_main.cpp")
if os.path.exists(app_main_path):
    with open(app_main_path, "r") as f:
        app_data = f.read()

    # Find the top of app_main() to inject the NVS boot sequence
    if "nvs_flash_init" not in app_data:
        pattern = re.compile(r'(void\s+app_main\s*\(\s*void\s*\)\s*\{)')
        
        replacement = r"""\1
    // ARCHITECT FIX: Initialize Non-Volatile Storage (NVS)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
"""
        app_data, count = pattern.subn(replacement, app_data, count=1)
        
        # Inject the #include at the top if missing
        if count > 0 and "nvs_flash.h" not in app_data:
            app_data = "#include \"nvs_flash.h\"\n" + app_data
            
        with open(app_main_path, "w") as f:
            f.write(app_data)
        print("Successfully injected NVS initialization into app_main.cpp")

print("Successfully established the Non-Volatile Calibration subsystem.")