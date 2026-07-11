idf_component_register(
    SRCS
        "app_main.c"
        "drivers/audio_loopback.c"
        "drivers/copet_ble.c"
        "drivers/touch_button.c"
    INCLUDE_DIRS "."
    REQUIRES
        bt
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_spi
        esp_lcd
        esp_timer
        nvs_flash
)
