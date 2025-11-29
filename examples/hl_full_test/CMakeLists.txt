add_executable(hl_full_test
    main.c
)

pico_enable_stdio_usb(hl_full_test 1)
pico_enable_stdio_uart(hl_full_test 0)

target_link_libraries(hl_full_test
    PRIVATE
    vfd_display
    pico_stdlib
    hardware_rtc
    hardware_adc
)

pico_add_extra_outputs(hl_full_test)