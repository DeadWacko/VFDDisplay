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
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
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
    # src/display.c
    src/display_ll.c
    src/display_core.c
    src/display_content.c
    src/display_font.c
    src/display_fx.c
    src/display_overlay.c
    src/display_rng.c
    src/display_lut.c
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
# add_subdirectory(examples/clock_stress_test)
add_subdirectory(examples/ll_test)


add_subdirectory(examples/clock_ll_basic) # <--- ДОБАВИТЬ ЭТУ СТРОКУ
add_subdirectory(examples/clock_hl_basic)
add_subdirectory(examples/hl_full_test)

# Test for Issue 13: clear alarm cancellation
add_executable(test_issue_13_clear_alarm
    examples/tests/test_issue_13_clear_alarm.c
)
pico_enable_stdio_usb(test_issue_13_clear_alarm 1)
pico_enable_stdio_uart(test_issue_13_clear_alarm 0)
target_link_libraries(test_issue_13_clear_alarm
    PRIVATE
        vfd_display
        pico_stdlib
        hardware_timer
        hardware_gpio
)
pico_add_extra_outputs(test_issue_13_clear_alarm)
