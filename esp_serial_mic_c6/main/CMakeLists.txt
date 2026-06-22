idf_component_register(SRCS "serial_mic_c6_main.c" "oled_eyes.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_adc esp_wifi esp_event esp_netif nvs_flash
                                esp_driver_gpio esp_driver_usb_serial_jtag
                                esp_driver_i2c)
