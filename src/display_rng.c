#include "display_rng.h"
#include "hardware/adc.h"
#include "pico/stdlib.h"

/*
 * RNG Implementation.
 * Реализация генератора псевдослучайных чисел на базе алгоритма XORSHIFT32.
 * Используется для создания недетерминированного поведения в визуальных эффектах.
 */

static uint32_t g_rng_state = 0x12345678;

/*
 * Генерация следующего 32-битного псевдослучайного числа.
 * Алгоритм: XORSHIFT32.
 */
uint32_t display_rng_next(void)
{
    uint32_t x = g_rng_state;
    x ^= (x << 13);
    x ^= (x >> 17);
    x ^= (x << 5);
    g_rng_state = x;
    return x;
}

/*
 * Установка начального значения генератора (Seed).
 * Значение 0 недопустимо для XORSHIFT, при передаче 0 используется fallback.
 */
void display_rng_seed(uint32_t seed)
{
    if (seed == 0) seed = 0xA5A5A5A5u;
    g_rng_state = seed;
}

/*
 * Инициализация генератора энтропией с АЦП.
 * Считывает шум младших бит на указанном пине ADC и смешивает с системным временем.
 */
void display_rng_seed_from_adc(uint16_t adc_pin)
{
    uint32_t acc = 0;
    uint8_t input = 0;

    // Преобразование GPIO (26-29) в канал ADC (0-3)
    if (adc_pin >= 26 && adc_pin <= 29) {
        input = (uint8_t)(adc_pin - 26);
    }

    adc_select_input(input);

    // Накопление шума
    for (uint8_t i = 0; i < 16; i++) {
        uint16_t v = adc_read();
        acc ^= ((uint32_t)v << (i & 7));
        sleep_us(120);
    }

    // Добавление временной метки
    uint32_t t = (uint32_t)to_us_since_boot(get_absolute_time());
    acc ^= t;

    display_rng_seed(acc);
}

/*
 * Получение случайного числа в заданном диапазоне [0 .. limit-1].
 */
uint32_t display_rng_range(uint32_t limit)
{
    if (limit == 0) return 0;
    return display_rng_next() % limit;
}