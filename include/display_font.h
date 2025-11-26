#ifndef DISPLAY_FONT_H
#define DISPLAY_FONT_H

#include <stdint.h>
#include "display_ll.h"   // для vfd_seg_t

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Общий модуль шрифта для индикатора.
 * Пока тут только цифры 0–9, но позже можно добавить буквы и спецсимволы.
 *
 * ВАЖНО:
 *  - это единственное место, где зашиты битовые маски сегментов под конкретный VFD;
 *  - HL и тесты используют только этот модуль, а не свои локальные таблицы.
 */

// Глобальный массив сегментных масок для цифр 0–9
extern const vfd_seg_t g_display_font_digits[10];

/** Удобный helper: вернуть маску сегментов для цифры (0..9). */
static inline vfd_seg_t display_font_digit(uint8_t d)
{
    if (d < 10) return g_display_font_digits[d];
    return g_display_font_digits[0];
}

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_FONT_H
