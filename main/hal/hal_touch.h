#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool touch_hal_read(int16_t *x, int16_t *y);

#ifdef __cplusplus
}
#endif
