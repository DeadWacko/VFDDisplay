#include <stdio.h>
#include "pico/stdlib.h"
#include "display_api.h"

// Простой тест нового HL-API:
// - инициализация дисплея
// - показ числа
// - показ времени
// - показ даты
// - показ текста (цифрового)
// Всё через display_api.h, без прямого доступа к LL.

static void delay_ms_blocking(uint32_t ms)
{
    // На будущее здесь можно будет дергать display_process() чаще,
    // пока эффекты/оверлеи не реализованы — просто sleep.
    sleep_ms(ms);
}

int main(void)
{
    stdio_init_all();
    sleep_ms(500);

    // Предполагаем 4 разряда, как у тебя сейчас
    display_init(4);

    // Максимальная яркость
    display_set_brightness(255);

    printf("HL test: init done, starting demo...\n");

    // --- Фаза 1: показываем числа 0..9 ---
    for (int32_t v = 0; v <= 9; v++) {
        display_show_number(v);
        for (int i = 0; i < 10; i++) {
            display_process();
            delay_ms_blocking(30);
        }
    }

    // --- Фаза 2: простое время 12:34 с двоеточием ---
    display_show_time(12, 34, true);
    for (int i = 0; i < 100; i++) {
        display_process();
        delay_ms_blocking(10);
    }

    // --- Фаза 3: дата 31.12 ---
    display_show_date(31, 12);
    for (int i = 0; i < 100; i++) {
        display_process();
        delay_ms_blocking(10);
    }

    // --- Фаза 4: текст "1234" ---
    display_show_text("1234");
    for (int i = 0; i < 100; i++) {
        display_process();
        delay_ms_blocking(10);
    }

    // --- Фаза 5: бесконечный счётчик времени ---
    uint8_t hours = 12;
    uint8_t minutes = 0;
    uint32_t tick = 0;

    while (true) {
        // Каждую секунду увеличиваем минуты (условный софт-RTC)
        if ((tick % 100) == 0) {
            minutes++;
            if (minutes >= 60) {
                minutes = 0;
                hours = (uint8_t)((hours + 1) % 24);
            }
            display_show_time(hours, minutes, true);
        }

        display_process();
        delay_ms_blocking(10);
        tick++;
    }
}
