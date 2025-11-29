add_executable(clock_hl_basic
    main.c
)

pico_enable_stdio_usb(clock_hl_basic 1)
pico_enable_stdio_uart(clock_hl_basic 0)

target_link_libraries(clock_hl_basic
    PRIVATE
    vfd_display
    pico_stdlib
    hardware_rtc 
    hardware_adc # Нужно, так как библиотека использует ADC для автояркости и RNG
)

pico_add_extra_outputs(clock_hl_basic)