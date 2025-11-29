#ifndef DISPLAY_RNG_H
#define DISPLAY_RNG_H

#include <stdint.h>
#include "pico/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Модуль генерации псевдослучайных чисел.
 * Реализует алгоритм Xorshift32. Используется для визуальных эффектов
 * (Glitch, Dissolve, Matrix), не требуя использования стандартной библиотеки rand().
 */

/* Инициализация генератора заданным значением (seed). */
void display_rng_seed(uint32_t seed);

/* Инициализация генератора случайным шумом с указанного вывода АЦП. */
void display_rng_seed_from_adc(uint16_t adc_pin);

/* Получение следующего 32-битного псевдослучайного числа. */
uint32_t display_rng_next(void);

/* Получение псевдослучайного числа в диапазоне [0 .. limit-1]. */
uint32_t display_rng_range(uint32_t limit);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_RNG_H