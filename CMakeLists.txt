cmake_minimum_required(VERSION 3.16)

# The board shown in the hardware photos is an ESP32-WROOM-32 DevKit, not S2/S3.
set(IDF_TARGET "esp32" CACHE STRING "ESP-IDF target")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(copet_hardware_test)
