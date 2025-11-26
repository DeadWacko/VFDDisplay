# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(VFDDisplay C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

#
# 1. Библиотека драйвера дисплея
#
#
# 1. Библиотека драйвера дисплея
#
add_library(vfd_display
    src/display.c
    src/display_ll.c
)

# Публичные заголовки
target_include_directories(vfd_display
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

# Общие дефайны для библиотеки
target_compile_definitions(vfd_display
    PUBLIC
        DEBUG_COLORS
)

# Подключаем Pico SDK-библиотеки, чтобы были доступны заголовки
# "pico/stdlib.h", "hardware/..." и чтобы далее примеры могли линковаться.
target_link_libraries(vfd_display
    PUBLIC
        pico_stdlib
        hardware_timer
        hardware_adc
        hardware_rtc
        hardware_sync
)


#
# 2. Примеры
#
add_subdirectory(examples/clock_stress_test)
add_subdirectory(examples/ll_test)
# На будущее:
# add_subdirectory(examples/clock_basic)
# add_subdirectory(examples/fx_showcase)
