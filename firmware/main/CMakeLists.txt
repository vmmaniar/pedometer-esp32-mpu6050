idf_component_register(
    SRCS "app_main.c" "step_detector.c"
    INCLUDE_DIRS "."
    REQUIRES mpu6050 ssd1306 driver nvs_flash
)
