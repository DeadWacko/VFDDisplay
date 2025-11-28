#include "display_rng.h"
#include "hardware/adc.h"
#include "pico/stdlib.h"

static uint32_t g_rng_state = 0x12345678;    // default (in case no seed)

/*
 * Classic XORSHIFT32:
 *
 *   x ^= x << 13;
 *   x ^= x >> 17;
 *   x ^= x << 5;
 *
 * Very fast and good enough for VFD FX randomness.
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

void display_rng_seed(uint32_t seed)
{
    if (seed == 0) seed = 0xA5A5A5A5u;  // avoid zero seed (bad for XORSHIFT)
    g_rng_state = seed;
}

void display_rng_seed_from_adc(uint16_t adc_pin)
{
    uint32_t acc = 0;

    // ADC pin must be 26..29 → ADC0..ADC3
    uint8_t input = 0;
    if (adc_pin >= 26 && adc_pin <= 29) {
        input = (uint8_t)(adc_pin - 26);
    }

    adc_select_input(input);

    // Take 16 noisy samples — OR-shift them into a seed
    for (uint8_t i = 0; i < 16; i++) {
        uint16_t v = adc_read();     // 0..4095
        acc ^= ((uint32_t)v << (i & 7));
        sleep_us(120);
    }

    uint32_t t = (uint32_t)to_us_since_boot(get_absolute_time());
    acc ^= t;

    display_rng_seed(acc);
}

uint32_t display_rng_range(uint32_t limit)
{
    if (limit == 0) return 0;
    return display_rng_next() % limit;
}
