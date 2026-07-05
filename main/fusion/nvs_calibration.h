#pragma once
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
