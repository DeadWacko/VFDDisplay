#ifndef DISPLAY_FONT_H
#define DISPLAY_FONT_H

#include <stdint.h>
#include "display_ll.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Модуль шрифтов.
 * Содержит паттерны сегментов для символов и цифр.
 */

/* Глобальный массив паттернов для цифр 0-9. */
extern const vfd_segment_map_t g_display_font_digits[10];

/*
 * Получение паттерна сегментов для цифры (0-9).
 * Возвращает 0 (пустоту), если значение выходит за пределы диапазона.
 */
static inline vfd_segment_map_t display_font_digit(uint8_t d)
{
    if (d < 10) return g_display_font_digits[d];
    return 0;
}

/* Получение паттерна сегментов для символа ASCII (A-Z, 0-9, спецсимволы). */
vfd_segment_map_t display_font_get_char(char c);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_FONT_H