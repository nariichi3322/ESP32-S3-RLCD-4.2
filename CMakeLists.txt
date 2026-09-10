# 配置天气时钟 ESP-IDF 工程版本、项目名和额外组件目录。
cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "v100.0.3")

# Build one language-specific App image at a time. The numeric ids are kept
# stable because ui_lang_v1 stores them in NVS.
set(APP_UI_LOCALE "zh-TW" CACHE STRING "App UI locale (zh-TW, zh-CN, en, ja)")
set_property(CACHE APP_UI_LOCALE PROPERTY STRINGS zh-TW zh-CN en ja)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS
    components/ExternLib/SensorLib
    components/ExternLib/esp_codec_dev
    components/ExternLib/codec_board)
project(weather_clock)
