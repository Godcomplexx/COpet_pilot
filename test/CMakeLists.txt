cmake_minimum_required(VERSION 3.20)

project(copet_ui C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

add_executable(
    desk_mode_tests
    desk_mode_test.c
    ../main/modes/desk_mode.c
)
target_include_directories(desk_mode_tests PRIVATE ../main)
add_test(NAME desk_mode_tests COMMAND desk_mode_tests)

include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 6.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(raylib)

add_executable(copet_ui main.c)

target_link_libraries(copet_ui PRIVATE raylib)
