#include "nvs_flash.h"
#include "esp_log.h"
#include "hal_imu.h"
#include "eskf_fusion.h"
#include "ui_render.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN_EXECUTOR";

extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ui_render_init();
    imu_hal_init();
    eskf_fusion_init();

    ESP_LOGI(TAG, "Architecture initialized. Starting multiplexed data pipeline...");

    while (1) {
        imu_9dof_data_t sensor_data;
        
        if (imu_hal_read_9dof(&sensor_data) == ESP_OK) {
            eskf_fusion_queue_data(&sensor_data);
        }
        
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}
