#ifndef DISPLAY_RNG_H
#define DISPLAY_RNG_H

#include <stdint.h>
#include "pico/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Immortal HL RNG — XORSHIFT32
 * ----------------------------
 * - deterministic
 * - fast
 * - 32-bit period (~4e9)
 * - no malloc, no float, no libc rand()
 *
 * Used only for FX-layer (glitch, dissolve, matrix, etc.)
 */

void     display_rng_seed(uint32_t seed);
void     display_rng_seed_from_adc(uint16_t adc_pin);
uint32_t display_rng_next(void);
uint32_t display_rng_range(uint32_t limit);   // returns [0 .. limit-1]

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_RNG_H
