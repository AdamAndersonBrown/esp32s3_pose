#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static inline void cores3_hardware_init(void) {
    ESP_LOGW("HARDWARE", "Executing 3000ms safety delay to prevent bricking...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI("HARDWARE", "Safety delay passed. Handing over to BSP...");
}
