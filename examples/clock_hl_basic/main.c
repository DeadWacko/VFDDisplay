#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/rtc.h"
#include "display_api.h"

// Конфигурация по умолчанию
#define DISPLAY_DIGITS 4

int main(void) {
    // 1. Инициализация Pico
    stdio_init_all();
    sleep_ms(2000);
    printf("=== VFD High-Level Basic Clock ===\n");

    // 2. Инициализация RTC
    rtc_init();
    datetime_t t = {
        .year  = 2025, .month = 1, .day   = 1,
        .dotw  = 3,    .hour  = 12, .min   = 0, .sec   = 0
    };
    rtc_set_datetime(&t);

    // 3. Инициализация дисплея
    display_init(DISPLAY_DIGITS);

    // Установка яркости (0..255)
    display_set_brightness(200);

    // Включаем автоматическое мигание разделительных точек (секунды)
    // Драйвер сам будет управлять точкой во 2-м разряде.
    display_set_dot_blinking(true);

    printf("Display initialized. Entering main loop...\n");

    while (true) {
        // 4.HL API: display_process()
        // Эту функцию нужно вызывать как можно чаще. Она крутит анимации,
        // обновляет яркость и обрабатывает оверлеи.
        display_process();

        // 5. Обновление контента
        // Не обязательно делать это каждый цикл, достаточно раз в 100мс
        static absolute_time_t last_update;
        absolute_time_t now = get_absolute_time();
        
        if (absolute_time_diff_us(last_update, now) > 100000) { // 100 ms
            last_update = now;
            
            rtc_get_datetime(&t);
            
            // Просто отдаем часы и минуты. 
            // Флаг show_colon=true означает, что мы разрешаем точке гореть,
            // но так как включен dot_blinking, она будет сама мигать.
            display_show_time(t.hour, t.min, true);
        }
    }
}