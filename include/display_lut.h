#ifndef DISPLAY_LUT_H
#define DISPLAY_LUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Immortal HL LUTs:
 * -----------------
 * - cos_lut[256]:  full 0..2π cycle, integer 0..255
 *   index = phase (0..255), output = (cos(x)*0.5+0.5)*255
 *
 * Мы используем готовую LUT, а не вычисляем.
 */

extern const uint8_t display_cos_lut[256];

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_LUT_H
