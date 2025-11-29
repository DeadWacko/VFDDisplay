# Создаем исполняемый файл для примера
add_executable(ll_test
    main.c
)

# ==========================================
# НАСТРОЙКА STDIO (ВЫВОД В КОНСОЛЬ)
# ==========================================
# Включаем вывод через USB (CDC UART)
pico_enable_stdio_usb(ll_test 1)

# Отключаем вывод через аппаратный UART (GP0/GP1), 
# чтобы printf работал только через USB кабель.
pico_enable_stdio_uart(ll_test 0)

# Линкуем с нашей библиотекой дисплея.
target_link_libraries(ll_test
    PRIVATE
    vfd_display
    pico_stdlib
)

# Генерация файлов .uf2, .hex, .bin
pico_add_extra_outputs(ll_test)