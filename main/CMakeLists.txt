idf_component_register(
    SRCS
                    "hal/hal_imu.cpp"
                    "fusion/eskf_fusion.cpp"
                    "fusion/nvs_calibration.cpp"
                    "ui/ui_render.cpp" 
        "core/app_main.cpp"
        "core/bmi270_context.c"
        "core/error_manager.c"
        "core/esp_logo.c"
        "core/esp_text.c"
        "core/graphics_support.cpp"
        "core/image_to_3d_matrix.c"
    INCLUDE_DIRS
                    "hal"
                    "fusion"
                    "ui" "." "core" "hardware"
)
