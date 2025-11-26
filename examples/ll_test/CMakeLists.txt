add_executable(ll_test
    main.c
)

target_link_libraries(ll_test
    vfd_display
    pico_stdlib
    hardware_gpio
    hardware_timer
)

pico_add_extra_outputs(ll_test)
