# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 让 LVGL 使用项目根目录下的 lv_conf.h
# （注意这里把路径作为字符串宏传递给编译器）
add_compile_definitions(LV_CONF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h")
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE=1)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(CD_Player)
