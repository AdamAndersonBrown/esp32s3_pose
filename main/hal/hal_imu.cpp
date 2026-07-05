#include "hal_imu.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "HAL_IMU";
static bool imu_initialized = false;

// Hardware Variance Watchdog State
static float last_mag[3] = {0.0f, 0.0f, 0.0f};

esp_err_t imu_hal_init(void) {
    ESP_LOGI(TAG, "Initializing 9-DoF Hardware Abstraction Layer...");
    // TODO: Migrate the BMM150 Wake/Suspend I2C writes here.
    imu_initialized = true;
    return ESP_OK;
}

esp_err_t imu_hal_read_9dof(imu_9dof_data_t *data) {
    if (!data || !imu_initialized) return ESP_ERR_INVALID_STATE;

    // 1. Execute the blocking I2C read from the BMI270 Gatekeeper
    // TODO: Drop your raw read_bmi270_data() polling execution here.
    
    // Placeholder variables for your raw parsed integer data
    float raw_mag_x = 0.0f; 
    float raw_mag_y = 0.0f;
    float raw_mag_z = 0.0f; 

    data->mag_x = raw_mag_x;
    data->mag_y = raw_mag_y;
    
    // 2. ARCHITECT FIX: Right-Hand Rule Z-Axis Parity Inversion
    // Invert Mag Z to mathematically align with Earth's downward gravity vector
    data->mag_z = -raw_mag_z;

    // 3. ARCHITECT FIX: Hardware Variance Watchdog
    // The BMM150 auxiliary bus can deadlock silently without throwing NACKs.
    // True analog magnetic fields ALWAYS exhibit analog noise. 
    // If the vector is mathematically identical to the last frame, the silicon is ghosting.
    if (data->mag_x == last_mag[0] && 
        data->mag_y == last_mag[1] && 
        data->mag_z == last_mag[2]) {
        data->mag_valid = false;
    } else {
        data->mag_valid = true;
        last_mag[0] = data->mag_x;
        last_mag[1] = data->mag_y;
        last_mag[2] = data->mag_z;
    }

    return ESP_OK;
}
