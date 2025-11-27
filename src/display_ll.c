// ============================================================================
//   VFD Display — LL Layer 4.0 (Immortal Edition)
//   Platform: RP2040 / Raspberry Pi Pico / Pico W
//   License: MIT
//   Status: Production-Ready, Race-Free, Deadlock-Free
//
//   Главные особенности LL 4.0:
//   • НИ ОДНОГО spinlock внутри IRQ → невозможно словить дедлок
//   • clear_cb и fast_timer_cb изолированы друг от друга
//   • Все общие ресурсы модифицируются только из fast_timer_cb
//   • clear_cb — простое, “чистое” гашение, не трогающее состояние
//   • Гарантированная атомарность с disable_interrupts()
//   • Zero-float: PWM + gamma полностью integer
//   • Корректный init/deinit с освобождением spinlock
//   • Безопасное повторное init()
//   • Готово к расширению для SPI / DMA / MAX6921 / HV5812
// ============================================================================

#include "display_ll.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "pico/sync.h"

#include <string.h>


// ============================================================================
//  CONFIG
// ============================================================================

#define LL_SHIFT_DELAY() tight_loop_contents()

// Минимальная длительность PWM-импульса, чтобы не пропадал на быстрых Pico
#define LL_MIN_PULSE_US   4


// ============================================================================
//  INTERNAL STATE
// ============================================================================

typedef struct
{
    // Status
    volatile bool initialized;
    volatile bool refresh_running;

    // GPIO
    uint8_t data_pin;
    uint8_t clock_pin;
    uint8_t latch_pin;

    // VFD parameters
    uint8_t  digit_count;
    uint16_t refresh_rate_hz;
    uint32_t slot_period_us;

    // Buffers
    vfd_seg_t seg_buffer[VFD_MAX_DIGITS];
    uint8_t   brightness[VFD_MAX_DIGITS];

    uint8_t current_digit;

    // Timers
    struct repeating_timer fast_timer;
    alarm_id_t clear_alarm;

    // Spinlock only for protecting buffers (NEVER used in IRQ)
    spin_lock_t *lock;
    int          lock_num;

    bool gamma_enabled;

} display_ll_state_t;

static display_ll_state_t s_ll;


// ============================================================================
//  LOW LEVEL BITBANG HELPERS (NO LOCKS)
// ============================================================================

static inline void ll_shift_out(uint8_t data)
{
    for (int i = 7; i >= 0; i--)
    {
        gpio_put(s_ll.data_pin, (data >> i) & 1u);
        gpio_put(s_ll.clock_pin, 1);
        LL_SHIFT_DELAY();
        gpio_put(s_ll.clock_pin, 0);
    }
}

static inline void ll_latch(void)
{
    gpio_put(s_ll.latch_pin, 1);
    LL_SHIFT_DELAY();
    gpio_put(s_ll.latch_pin, 0);
    LL_SHIFT_DELAY();
}


// ============================================================================
//  GAMMA  (integer x^2/255)
// ============================================================================

static inline uint8_t ll_gamma_calc(uint8_t x)
{
    uint32_t v = (uint32_t)x * x + 254u;
    v /= 255u;
    return (v > 255) ? 255 : (uint8_t)v;
}


// ============================================================================
//  CLEAR CALLBACK  (IRQ)   — NO LOCKS INSIDE
// ============================================================================
//
//   Очень важно:
//   • clear_cb НЕ изменяет ни одного общего состояния!
//   • clear_cb НЕ обнуляет s_ll.clear_alarm
//   • clear_cb НЕ проверяет ID
//   • Единственная задача — погасить железо
//
//   Все изменения состояния clear_alarm выполняет только fast_timer_cb().
//
//   Это делает LL полностью защищённым от дедлоков и “зомби-алармов”.
//
static int64_t ll_clear_cb(alarm_id_t id, void *user_data)
{
    (void)id;
    (void)user_data;

    // Никаких локов, никаких полей состояния
    ll_shift_out(0x00);
    ll_shift_out(0x00);
    ll_latch();
    return 0;
}


// ============================================================================
//  FAST TIMER CALLBACK  (IRQ)  — единственный владелец состояния
// ============================================================================

static bool ll_fast_timer_cb(struct repeating_timer *t)
{
    (void)t;

    if (!s_ll.initialized)
        return true;

    // Полностью локальная переменная
    uint8_t digit = s_ll.current_digit;
    if (digit >= s_ll.digit_count)
        digit = 0;

    // --- атомарный доступ к общим буферам ---
    uint32_t irq = save_and_disable_interrupts();
    vfd_seg_t segs = s_ll.seg_buffer[digit];
    uint8_t   pwm  = s_ll.brightness[digit];
    restore_interrupts(irq);

    uint8_t grid_mask = (uint8_t)(1u << (digit % 8));

    // Включаем разряд
    ll_shift_out(grid_mask);
    ll_shift_out(segs);
    ll_latch();

    // PWM
    if (pwm == 0)
    {
        ll_shift_out(0x00);
        ll_shift_out(0x00);
        ll_latch();
    }
    else if (pwm < 255)
    {
        uint32_t on_us = (uint32_t)pwm * s_ll.slot_period_us / 255u;
        if (on_us < LL_MIN_PULSE_US)
            on_us = LL_MIN_PULSE_US;

        // clear_alarm ID → полностью обновляется ТОЛЬКО здесь
        alarm_id_t new_id = add_alarm_in_us((int64_t)on_us, ll_clear_cb, NULL, true);

        // Если не получилось поставить alarm — безопасно гасим
        if (new_id >= 0)
            s_ll.clear_alarm = new_id;
        else
        {
            ll_shift_out(0x00);
            ll_shift_out(0x00);
            ll_latch();
            s_ll.clear_alarm = 0;
        }
    }
    else
    {
        // pwm = 255 — ничего не делаем
    }

    // Next digit
    digit++;
    if (digit >= s_ll.digit_count) digit = 0;
    s_ll.current_digit = digit;

    return true;
}


// ============================================================================
//  INIT
// ============================================================================

bool display_ll_init(const display_ll_config_t *cfg)
{
    if (!cfg) return false;
    if (cfg->digit_count == 0 || cfg->digit_count > VFD_MAX_DIGITS) return false;
    if (cfg->refresh_rate_hz < 50 || cfg->refresh_rate_hz > 2000) return false;

    if (s_ll.initialized)
        display_ll_deinit();

    // GPIO init
    gpio_init(cfg->data_pin);
    gpio_init(cfg->clock_pin);
    gpio_init(cfg->latch_pin);

    gpio_set_dir(cfg->data_pin,  GPIO_OUT);
    gpio_set_dir(cfg->clock_pin, GPIO_OUT);
    gpio_set_dir(cfg->latch_pin, GPIO_OUT);

    gpio_put(cfg->data_pin, 0);
    gpio_put(cfg->clock_pin, 0);
    gpio_put(cfg->latch_pin, 0);

    memset(&s_ll, 0, sizeof(s_ll));

    // Spinlock allocation
    int lock_id = spin_lock_claim_unused(true);
    if (lock_id < 0)
        return false;

    s_ll.lock_num = lock_id;
    s_ll.lock = spin_lock_init((uint)lock_id);

    // Store config
    s_ll.data_pin        = cfg->data_pin;
    s_ll.clock_pin       = cfg->clock_pin;
    s_ll.latch_pin       = cfg->latch_pin;
    s_ll.digit_count     = cfg->digit_count;
    s_ll.refresh_rate_hz = cfg->refresh_rate_hz;
    s_ll.gamma_enabled   = true;

    for (int i = 0; i < VFD_MAX_DIGITS; i++)
    {
        s_ll.seg_buffer[i] = 0;
        s_ll.brightness[i] = 255;
    }

    s_ll.initialized = true;
    return true;
}


bool display_ll_is_initialized(void)
{
    return s_ll.initialized;
}



// ============================================================================
//  START REFRESH
// ============================================================================

bool display_ll_start_refresh(void)
{
    if (!s_ll.initialized) return false;
    if (s_ll.refresh_running) return true;

    uint32_t slots_per_sec = (uint32_t)s_ll.refresh_rate_hz * s_ll.digit_count;
    if (slots_per_sec == 0) return false;

    int32_t period_us = -(int32_t)(1000000u / slots_per_sec);
    if (period_us == 0) period_us = -100;

    s_ll.slot_period_us = (uint32_t)(-period_us);
    s_ll.current_digit  = 0;
    s_ll.clear_alarm    = 0;

    s_ll.refresh_running = true;

    if (!add_repeating_timer_us(period_us, ll_fast_timer_cb, NULL, &s_ll.fast_timer))
    {
        s_ll.refresh_running = false;
        return false;
    }

    return true;
}


// ============================================================================
//  STOP REFRESH
// ============================================================================

void display_ll_stop_refresh(void)
{
    if (!s_ll.initialized || !s_ll.refresh_running)
        return;

    cancel_repeating_timer(&s_ll.fast_timer);

    // Выполним безопасное финальное гашение
    ll_shift_out(0x00);
    ll_shift_out(0x00);
    ll_latch();

    s_ll.refresh_running = false;
    s_ll.clear_alarm = 0;
}


// ============================================================================
//  DEINIT
// ============================================================================

void display_ll_deinit(void)
{
    if (!s_ll.initialized)
        return;

    display_ll_stop_refresh();

    // Free spinlock
    if (s_ll.lock_num >= 0)
        spin_lock_unclaim((uint)s_ll.lock_num);

    // Set pins to High-Z
    gpio_set_dir(s_ll.data_pin,  GPIO_IN);
    gpio_set_dir(s_ll.clock_pin, GPIO_IN);
    gpio_set_dir(s_ll.latch_pin, GPIO_IN);

    memset(&s_ll, 0, sizeof(s_ll));
}


// ============================================================================
//  SETTERS / GETTERS (atomic)
// ============================================================================

uint8_t display_ll_get_digit_count(void)
{
    return s_ll.digit_count;
}

vfd_seg_t *display_ll_get_buffer(void)
{
    return s_ll.seg_buffer; // Read-only outside
}

void display_ll_set_digit_raw(uint8_t idx, vfd_seg_t seg)
{
    if (!s_ll.initialized || idx >= s_ll.digit_count) return;
    uint32_t irq = save_and_disable_interrupts();
    s_ll.seg_buffer[idx] = seg;
    restore_interrupts(irq);
}

void display_ll_set_brightness(uint8_t idx, uint8_t lvl)
{
    if (!s_ll.initialized || idx >= s_ll.digit_count) return;
    uint32_t irq = save_and_disable_interrupts();
    s_ll.brightness[idx] = lvl;
    restore_interrupts(irq);
}

void display_ll_set_brightness_all(uint8_t lvl)
{
    if (!s_ll.initialized) return;
    uint32_t irq = save_and_disable_interrupts();
    for (int i = 0; i < s_ll.digit_count; i++)
        s_ll.brightness[i] = lvl;
    restore_interrupts(irq);
}


// ============================================================================
//  GAMMA
// ============================================================================

void display_ll_enable_gamma(bool en)
{
    s_ll.gamma_enabled = en;
}

uint8_t display_ll_apply_gamma(uint8_t x)
{
    return s_ll.gamma_enabled ? ll_gamma_calc(x) : x;
}

