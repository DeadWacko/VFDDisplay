# Пример: суточный стресс-тест часов и эффектов

add_executable(clock_stress_test
    main.c
)

# Имя программы (то, что видно в USB / бинарной инфе)
pico_set_program_name(clock_stress_test "VFD Clock Stress Test")
pico_set_program_version(clock_stress_test "0.1")

# Включаем/выключаем вывод через UART/USB
pico_enable_stdio_uart(clock_stress_test 1)
pico_enable_stdio_usb(clock_stress_test 0)

# Линковка с Pico SDK и нашей библиотекой дисплея
target_link_libraries(clock_stress_test
    pico_stdlib
    hardware_rtc
    hardware_spi
    hardware_adc
    pico_multicore
    vfd_display
)


# target_compile_definitions(clock_stress_test PRIVATE DEBUG_COLORS)

# Генерируем .uf2 и прочие выходные файлы
pico_add_extra_outputs(clock_stress_test)
