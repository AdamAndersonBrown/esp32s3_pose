#pragma once
#include "eskf_fusion.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the LVGL 3D rendering objects.
 */
void ui_render_init(void);

/**
 * @brief Translates the quaternion to a rotation matrix and updates the screen.
 *        Handles all FreeRTOS UI mutex locking internally.
 * @param q The orientation quaternion from the physics engine.
 */
void ui_render_update_3d(quaternion_t *q);

#ifdef __cplusplus
}
#endif
