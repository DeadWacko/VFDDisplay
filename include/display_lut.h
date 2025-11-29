#ifndef DISPLAY_LUT_H
#define DISPLAY_LUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Таблица предвычисленных значений косинуса (Look-Up Table).
 * Используется для создания плавных анимаций яркости (Pulse, Wave)
 * без использования вычислений с плавающей точкой.
 *
 * Размер: 256 байт.
 * Значения: (cos(x) + 1) / 2 * 255.
 */
extern const uint8_t display_cos_lut[256];

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_LUT_H