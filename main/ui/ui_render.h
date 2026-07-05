#pragma once
#include "eskf_fusion.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_render_init(void);
void ui_render_update_3d(quaternion_t *q, bool is_deadlocked);

#ifdef __cplusplus
}
#endif
