idf_component_register(SRCS
    "core/app_main.cpp"
    "hardware/display_manager.c"
    "hardware/display_cube.c"
    "ml/eskf_fusion.c"
    INCLUDE_DIRS
    "."
    "core"
    "hardware"
    "ml"
    PRIV_REQUIRES esp-dsp esp_lcd_ili9341 driver esp_lcd
)
