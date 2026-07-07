idf_component_register(
    SRCS
                    "hal/hal_imu.cpp"
                    "hal/hal_touch.cpp"
                    "fusion/eskf_fusion.cpp"
                    "fusion/calibration.cpp"
                    "fusion/kinematics.cpp"
                    "fusion/odometry.cpp"
                    "fusion/nvs_calibration.cpp"
                    "ui/ui_render.cpp" 
        "core/app_main.cpp"
        "core/bmi270_context.c"
        "core/graphics_support.cpp"
        INCLUDE_DIRS
                    "hal"
                    "fusion"
                    "ui" "." "core" "hardware"
)