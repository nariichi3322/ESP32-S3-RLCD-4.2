# 配置 LVGL SDL 模拟器的本地构建目标和依赖。
cmake_minimum_required(VERSION 3.16)
project(weather_clock_sdl_preview C CXX)

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt"
    WEATHER_CLOCK_PROJECT_VER_LINE
    REGEX "^set\\(PROJECT_VER \"v[0-9]+\\.[0-9]+\\.[0-9]+\"\\)$")
list(LENGTH WEATHER_CLOCK_PROJECT_VER_LINE WEATHER_CLOCK_PROJECT_VER_LINE_COUNT)
if(NOT WEATHER_CLOCK_PROJECT_VER_LINE_COUNT EQUAL 1)
    message(FATAL_ERROR "Unable to read one PROJECT_VER from the root CMakeLists.txt")
endif()
string(REGEX REPLACE
    "^set\\(PROJECT_VER \"([^\"]+)\"\\)$"
    "\\1"
    WEATHER_CLOCK_PROJECT_VER
    "${WEATHER_CLOCK_PROJECT_VER_LINE}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LV_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(LV_LVGL_H_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)

add_subdirectory("../managed_components/lvgl__lvgl" "${CMAKE_BINARY_DIR}/lvgl" EXCLUDE_FROM_ALL)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
endif()
if(NOT SDL2_FOUND)
    find_program(SDL2_CONFIG_EXECUTABLE NAMES sdl2-config REQUIRED)
    execute_process(
        COMMAND "${SDL2_CONFIG_EXECUTABLE}" --cflags
        OUTPUT_VARIABLE SDL2_CFLAGS_TEXT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE SDL2_CFLAGS_RESULT)
    execute_process(
        COMMAND "${SDL2_CONFIG_EXECUTABLE}" --libs
        OUTPUT_VARIABLE SDL2_LDFLAGS_TEXT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE SDL2_LDFLAGS_RESULT)
    if(NOT SDL2_CFLAGS_RESULT EQUAL 0 OR NOT SDL2_LDFLAGS_RESULT EQUAL 0)
        message(FATAL_ERROR "sdl2-config failed to provide compiler or linker flags")
    endif()
    separate_arguments(SDL2_CFLAGS_OTHER NATIVE_COMMAND "${SDL2_CFLAGS_TEXT}")
    separate_arguments(SDL2_LDFLAGS NATIVE_COMMAND "${SDL2_LDFLAGS_TEXT}")
    set(SDL2_INCLUDE_DIRS "")
endif()

add_executable(weather_clock_sdl
    "main.cpp"
    "sdl_preview_backend.cpp"
    "sdl_preview_boot.cpp"
    "sdl_preview_calendar.cpp"
    "sdl_preview_clock.cpp"
    "sdl_preview_codex.cpp"
    "sdl_preview_flip_cards.cpp"
    "sdl_preview_flip_clock.cpp"
    "sdl_preview_gallery.cpp"
    "sdl_preview_history.cpp"
    "sdl_preview_progress.cpp"
    "sdl_preview_settings.cpp"
    "sdl_preview_weather.cpp"
    "sdl_preview_widgets.cpp"
    "sdl_preview_work_status.cpp"
    "sdl_preview_xiaozhi.cpp"
    "../main/ui/ui_dseg_render.cpp"
    "../main/assets/boot_anim.c"
    "../main/assets/qweather_icons_36.c"
    "../main/assets/zh_font_16.c"
    "../main/assets/zh_flip_lunar_22.c"
    "../main/assets/zh_pomodoro_title_24.c")

target_include_directories(weather_clock_sdl PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "../main"
    "../main/assets"
    "../main/ui"
    ${SDL2_INCLUDE_DIRS})

target_compile_options(weather_clock_sdl PRIVATE ${SDL2_CFLAGS_OTHER})
target_compile_definitions(weather_clock_sdl PRIVATE
    WEATHER_CLOCK_PROJECT_VER="${WEATHER_CLOCK_PROJECT_VER}")
target_link_options(weather_clock_sdl PRIVATE ${SDL2_LDFLAGS})
target_link_libraries(weather_clock_sdl PRIVATE lvgl)
