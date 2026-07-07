#pragma once
#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    float acc_x, acc_y, acc_z;
    float gyr_x, gyr_y, gyr_z;
    float mag_x, mag_y, mag_z;
    bool mag_valid;
} imu_9dof_data_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t imu_hal_init(void);
esp_err_t imu_hal_read_9dof(imu_9dof_data_t *data);


#ifdef __cplusplus
}
#endif
