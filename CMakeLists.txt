cmake_minimum_required(VERSION 3.20.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stm32f407_zephyr C)

set(fatfs Middlewares/Third_Party/FatFs/src)
set(usb Middlewares/ST/STM32_USB_Device_Library)
set(lvgl Middlewares/Third_Party/lvgl)
file(GLOB board_sources CONFIGURE_DEPENDS Core/Src/*.c FATFS/App/*.c FATFS/Target/*.c
     USB_DEVICE/App/*.c USB_DEVICE/Target/*.c ${usb}/Core/Src/*.c ${usb}/Class/CDC/Src/*.c)
file(GLOB_RECURSE lvgl_sources CONFIGURE_DEPENDS ${lvgl}/src/*.c)
target_sources(app PRIVATE ${board_sources} ${lvgl_sources}
    ${fatfs}/diskio.c ${fatfs}/ff.c ${fatfs}/ff_gen_drv.c
    ${fatfs}/option/ccsbcs.c ${fatfs}/option/syscall.c)
target_include_directories(app PRIVATE Core/Inc FATFS/App FATFS/Target ${fatfs}
    USB_DEVICE/App USB_DEVICE/Target ${usb}/Core/Inc ${usb}/Class/CDC/Inc ${lvgl})
target_compile_definitions(app PRIVATE LV_CONF_INCLUDE_SIMPLE)
