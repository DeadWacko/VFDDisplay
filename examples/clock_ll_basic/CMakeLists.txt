add_executable(clock_ll_basic
    main.c
)

# Включаем вывод в USB (чтобы видеть printf "RTC Initialized")
pico_enable_stdio_usb(clock_ll_basic 1)
pico_enable_stdio_uart(clock_ll_basic 0)

target_link_libraries(clock_ll_basic
    PRIVATE
    vfd_display
    pico_stdlib
    hardware_rtc  
)

pico_add_extra_outputs(clock_ll_basic)