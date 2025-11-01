# Collect all .c files in this component
file(GLOB_RECURSE SOURCES *.c)

idf_component_register(
    SRCS        ${SOURCES}
    INCLUDE_DIRS "."
    # 这里声明本组件需要的依赖，确保能找到 FreeRTOS/USB 等头文件与库
    REQUIRES    freertos usb driver esp_timer
)
