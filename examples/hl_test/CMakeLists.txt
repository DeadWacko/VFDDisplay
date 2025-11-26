add_executable(hl_test
    main.c
)

target_link_libraries(hl_test
    vfd_display
    pico_stdlib
    hardware_gpio
    hardware_timer
)

pico_add_extra_outputs(hl_test)
