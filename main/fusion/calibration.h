#pragma once
#include "nvs_calibration.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void calibration_init(void);
void calibration_trigger(void);
bool calibration_is_active(void);
bool calibration_process_sample(float mx, float my, float mz);
void calibration_apply(float* hx, float* hy, float* hz);

#ifdef __cplusplus
}
#endif
