#include <stdio.h>
#include "pico/stdlib.h"
#include "display_ll.h"

// ВАЖНО:
// Здесь НЕТ связи с библиотечными CHAR_MAP — это ЛОКАЛЬНАЯ таблица только для теста LL,
// чтобы не нарушать правило "нет CHAR_MAP в библиотеке".
//
// ПОДСТАВЬ сюда свои реальные значения, КАК В clock_stress_test:
//
//   cfg.data_pin         → VFD_DATA_PIN
//   cfg.clock_pin        → VFD_CLOCK_PIN
//   cfg.latch_pin        → VFD_LATCH_PIN
//   cfg.digit_count      → VFD_DIGIT_COUNT
//   cfg.fast_refresh_rate→ VFD_REFRESH_HZ
//
#define VFD_DATA_PIN      15
#define VFD_CLOCK_PIN     14
#define VFD_LATCH_PIN     13
#define VFD_DIGIT_COUNT   4
#define VFD_REFRESH_HZ    120

// ЛОКАЛЬНАЯ 7-сегментная таблица цифр 0–9 (bit0=a, bit1=b, ..., bit6=g, bit7=dp)
// Это классическая common-cathode раскладка.
// На твоём VFD порядок сегментов может отличаться — тогда цифры будут чуть "кривые",
// но ЗАТО поведение LL (обновление, PWM, гамма) будет видно как есть.
static const uint8_t NUM_MAP[10] = {
    0b01111011, 0b00000011, 0b01011110, 0b01001111, 0b00100111,
    0b01101101, 0b01111101, 0b01000011, 0b01111111, 0b01101111
   
};

static inline uint8_t clamp_digit(uint8_t d)
{
    return (d < 10) ? d : 0;
}

// Показать одну цифру на конкретной позиции (pos = 0..digit_count-1)
static void ll_show_digit(uint8_t pos, uint8_t digit)
{
    uint8_t d = clamp_digit(digit);
    vfd_seg_t segmask = NUM_MAP[d];
    display_ll_set_digit_raw(pos, segmask);
}

// Показать целое число value справа, с заданным количеством разрядов.
// Левые разряды заполняются нулями.
static void ll_show_number_right_aligned(int value, uint8_t total_digits)
{
    if (total_digits == 0) return;

    if (value < 0) {
        value = -value; // на всякий случай
    }

    for (int i = 0; i < total_digits; i++) {
        uint8_t digit = (uint8_t)(value % 10);
        value /= 10;

        uint8_t pos = (uint8_t)(total_digits - 1 - i);
        ll_show_digit(pos, digit);
    }
}

// Небольшой тест: сначала показ 0–9, потом счётчик с дыханием яркости
int main(void)
{
    stdio_init_all();
    sleep_ms(500); // дать времени USB/питанию стабилизироваться

    display_ll_config_t cfg = {
        .data_pin        = VFD_DATA_PIN,
        .clock_pin       = VFD_CLOCK_PIN,
        .latch_pin       = VFD_LATCH_PIN,
        .digit_count     = VFD_DIGIT_COUNT,
        .refresh_rate_hz = VFD_REFRESH_HZ,
    };

    if (!display_ll_init(&cfg)) {
        printf("LL init FAILED\n");
        while (true) {
            tight_loop_contents();
        }
    }

    if (!display_ll_start_refresh()) {
        printf("LL start_refresh FAILED\n");
        while (true) {
            tight_loop_contents();
        }
    }

    printf("LL init OK, starting pretty digit test...\n");

    const uint8_t digits = display_ll_get_digit_count();

    // --- ФАЗА 1: прогоняем цифры 0..9 на всех разрядах (поочерёдно) ---
    display_ll_set_brightness_all(VFD_MAX_BRIGHTNESS);
    for (uint8_t d = 0; d < 10; d++) {
        for (uint8_t pos = 0; pos < digits; pos++) {
            ll_show_digit(pos, d);
        }
        sleep_ms(300);
    }

    // --- ФАЗА 2: статическая "1234..." на максимум, 1 сек ---
    for (uint8_t pos = 0; pos < digits; pos++) {
        ll_show_digit(pos, (uint8_t)((pos + 1) % 10)); // 1,2,3,4,...
    }
    display_ll_set_brightness_all(VFD_MAX_BRIGHTNESS);
    sleep_ms(1000);

    // --- ФАЗА 3: бесконечный счётчик + дыхание яркости через гамму ---
    uint8_t level = 0;
    int8_t  dir = 1;
    uint32_t counter = 0;
    uint32_t tick = 0;

    while (true) {
        // Обновляем число не каждый тик, чтобы было читаемо
        if ((tick % 10) == 0) {
            ll_show_number_right_aligned((int)(counter % 10000), digits);
            counter++;
        }

        // Дыхание яркости 0..255 с гаммой
        level = (uint8_t)(level + dir);
        if (level == 0 || level == 255) {
            dir = (int8_t)-dir;
        }

        uint8_t gamma_level = display_ll_apply_gamma(level);
        display_ll_set_brightness_all(gamma_level);

        tick++;
        sleep_ms(16); // ~60 кадров в секунду
    }
}
