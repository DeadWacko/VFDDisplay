#ifndef DISPLAY_FONT_H
#define DISPLAY_FONT_H

#include <stdint.h>
#include "display_ll.h" // чтобы знать тип vfd_seg_t

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Глобальный массив масок (определен в display_font.c)
 */
extern const vfd_seg_t g_display_font_digits[10];

/*
 * Хелпер для цифр (инлайн, чтобы было быстро)
 */
static inline vfd_seg_t display_font_digit(uint8_t d)
{
    // Если d > 9, возвращаем 0 (пустоту), или можно g_display_font_digits[0]
    if (d < 10) return g_display_font_digits[d];
    return 0; // Возвращаем пустоту для безопасности
}

/*
 * Получить маску сегментов для символа ASCII (буквы, цифры, знаки)
 * Реализация в display_font.c
 */
vfd_seg_t display_font_get_char(char c);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_FONT_H