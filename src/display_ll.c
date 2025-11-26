#include "display_ll.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/sync.h"

#include <string.h>

/* =====================
 *   ВНУТРЕННЕЕ СОСТОЯНИЕ
 * ===================== */

typedef struct {
    bool initialized;
    bool refresh_running;

    uint8_t data_pin;
    uint8_t clock_pin;
    uint8_t latch_pin;

    uint8_t digit_count;
    uint16_t refresh_rate_hz;

    vfd_seg_t seg_buffer[VFD_MAX_DIGITS];
    uint8_t brightness[VFD_MAX_DIGITS];   // 0..VFD_MAX_BRIGHTNESS

    uint8_t current_digit;

    bool gamma_enabled;

    struct repeating_timer fast_timer;
    alarm_id_t clear_alarm;

    spin_lock_t *lock;
} display_ll_state_t;

static display_ll_state_t s_ll = {
    .initialized      = false,
    .refresh_running  = false,
    .data_pin         = 0,
    .clock_pin        = 0,
    .latch_pin        = 0,
    .digit_count      = 0,
    .refresh_rate_hz  = 120,
    .current_digit    = 0,
    .gamma_enabled    = true,
    .clear_alarm      = 0,
    .lock             = NULL,
};

/* Максимальное "время свечения" одного разряда в микро-секундах.
 * При refresh ≈ 100–200 Гц это даёт комфортный duty-cycle. */
#define LL_SLOT_MAX_US  2000u

/* =====================
 *   ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
 * ===================== */

static inline void ll_shift_out(uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        bool bit = (data >> (7 - i)) & 0x1;
        gpio_put(s_ll.data_pin, bit);
        gpio_put(s_ll.clock_pin, 1);
        gpio_put(s_ll.clock_pin, 0);
    }
}

static inline void ll_latch(void)
{
    gpio_put(s_ll.latch_pin, 1);
    gpio_put(s_ll.latch_pin, 0);
}

static int64_t ll_clear_cb(alarm_id_t id, void *user_data)
{
    (void)id;
    (void)user_data;

    if (!s_ll.initialized)
        return 0;

    // Гасим все сегменты
    ll_shift_out(0x00);                  // сегменты
    ll_shift_out(0x00);                  // маска разрядов
    ll_latch();

    s_ll.clear_alarm = 0;
    return 0;
}

/* Один шаг мультиплексирования: выбираем разряд, выводим его, ставим будильник на гашение. */
static void ll_update(void)
{
    if (!s_ll.initialized || s_ll.digit_count == 0)
        return;

    uint32_t flags = spin_lock_blocking(s_ll.lock);

    uint8_t digit = s_ll.current_digit;
    if (digit >= s_ll.digit_count) {
        digit = 0;
        s_ll.current_digit = 0;
    }

    /* Формируем маску выбора разряда (1 горячий бит). */
    uint8_t digit_mask = (uint8_t)(1u << digit);

    /* Берём сырую маску сегментов. */
    vfd_seg_t segmask = s_ll.seg_buffer[digit];

    /* Выводим: сначала маску разряда, потом сегменты (или наоборот — зависит от железа;
     * здесь придерживаемся того же порядка, что и в старом display.c: сначала digit, потом segments). */
    ll_shift_out(digit_mask);
    ll_shift_out(segmask);
    ll_latch();

    /* Планируем, когда погасить разряд по яркости. */
    uint8_t level = s_ll.brightness[digit];
    uint32_t on_time_us = (uint32_t)level * LL_SLOT_MAX_US / VFD_MAX_BRIGHTNESS;

    if (s_ll.clear_alarm) {
        cancel_alarm(s_ll.clear_alarm);
        s_ll.clear_alarm = 0;
    }

    if (on_time_us > 0) {
        s_ll.clear_alarm = add_alarm_in_us((int64_t)on_time_us, ll_clear_cb, NULL, true);
    }

    /* Переходим к следующему разряду. */
    s_ll.current_digit = (uint8_t)((digit + 1) % s_ll.digit_count);

    spin_unlock(s_ll.lock, flags);
}

static bool ll_fast_timer_cb(struct repeating_timer *t)
{
    (void)t;
    ll_update();
    return true;    // продолжаем таймер
}

/* =====================
 *   ПУБЛИЧНЫЙ API
 * ===================== */

bool display_ll_init(const display_ll_config_t *cfg)
{
    if (!cfg)
        return false;

    if (cfg->digit_count == 0 || cfg->digit_count > VFD_MAX_DIGITS)
        return false;

    if (cfg->refresh_rate_hz == 0)
        return false;

    /* Настраиваем GPIO. */
    gpio_init(cfg->data_pin);
    gpio_init(cfg->clock_pin);
    gpio_init(cfg->latch_pin);

    gpio_set_dir(cfg->data_pin,  GPIO_OUT);
    gpio_set_dir(cfg->clock_pin, GPIO_OUT);
    gpio_set_dir(cfg->latch_pin, GPIO_OUT);

    /* Захватываем spin-lock. */
    int lock_num = spin_lock_claim_unused(true);
    if (lock_num < 0)
        return false;

    s_ll.lock = spin_lock_init(lock_num);

    s_ll.data_pin        = cfg->data_pin;
    s_ll.clock_pin       = cfg->clock_pin;
    s_ll.latch_pin       = cfg->latch_pin;
    s_ll.digit_count     = cfg->digit_count;
    s_ll.refresh_rate_hz = cfg->refresh_rate_hz;
    s_ll.current_digit   = 0;
    s_ll.gamma_enabled   = true;
    s_ll.clear_alarm     = 0;

    memset(s_ll.seg_buffer, 0x00, sizeof(s_ll.seg_buffer));
    for (int i = 0; i < VFD_MAX_DIGITS; i++) {
        s_ll.brightness[i] = VFD_MAX_BRIGHTNESS;
    }

    s_ll.initialized     = true;
    s_ll.refresh_running = false;

    return true;
}

bool display_ll_is_initialized(void)
{
    return s_ll.initialized;
}

bool display_ll_start_refresh(void)
{
    if (!s_ll.initialized)
        return false;

    if (s_ll.refresh_running)
        return true;

    /* Период таймера: один "слот" на разряд. */
    uint32_t slots_per_second = (uint32_t)s_ll.refresh_rate_hz * s_ll.digit_count;
    if (slots_per_second == 0)
        return false;

    int32_t period_us = (int32_t)(1000000u / slots_per_second);
    if (period_us <= 0)
        period_us = 1000;   // fallback ~1 kHz

    bool ok = add_repeating_timer_us(-period_us, ll_fast_timer_cb, NULL, &s_ll.fast_timer);
    if (!ok)
        return false;

    s_ll.refresh_running = true;
    return true;
}

void display_ll_stop_refresh(void)
{
    if (!s_ll.initialized || !s_ll.refresh_running)
        return;

    cancel_repeating_timer(&s_ll.fast_timer);
    s_ll.refresh_running = false;

    if (s_ll.clear_alarm) {
        cancel_alarm(s_ll.clear_alarm);
        s_ll.clear_alarm = 0;
    }

    /* Погасим дисплей. */
    ll_shift_out(0x00);
    ll_shift_out(0x00);
    ll_latch();
}

/* --- буфер сегментов --- */

uint8_t display_ll_get_digit_count(void)
{
    return s_ll.digit_count;
}

vfd_seg_t *display_ll_get_buffer(void)
{
    return s_ll.seg_buffer;
}

void display_ll_set_digit_raw(uint8_t index, vfd_seg_t segmask)
{
    if (!s_ll.initialized)
        return;

    if (index >= s_ll.digit_count)
        return;

    uint32_t flags = spin_lock_blocking(s_ll.lock);
    s_ll.seg_buffer[index] = segmask;
    spin_unlock(s_ll.lock, flags);
}

/* --- яркость --- */

void display_ll_set_brightness(uint8_t index, uint8_t level)
{
    if (!s_ll.initialized)
        return;

    if (index >= s_ll.digit_count)
        return;

    uint32_t flags = spin_lock_blocking(s_ll.lock);
    s_ll.brightness[index] = level;
    spin_unlock(s_ll.lock, flags);
}

void display_ll_set_brightness_all(uint8_t level)
{
    if (!s_ll.initialized)
        return;

    uint32_t flags = spin_lock_blocking(s_ll.lock);
    for (int i = 0; i < s_ll.digit_count; i++) {
        s_ll.brightness[i] = level;
    }
    spin_unlock(s_ll.lock, flags);
}

/* --- гамма-коррекция --- */

uint8_t display_ll_apply_gamma(uint8_t linear)
{
    if (!s_ll.gamma_enabled)
        return linear;

    /* Простейшая гамма ≈ 2.0 */
    float x = (float)linear / 255.0f;
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;

    x = x * x; // gamma 2.0

    uint32_t out = (uint32_t)(x * 255.0f + 0.5f);
    if (out > 255u) out = 255u;
    return (uint8_t)out;
}

void display_ll_enable_gamma(bool enable)
{
    s_ll.gamma_enabled = enable;
}
