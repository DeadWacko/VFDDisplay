#include "display_ll.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/sync.h"

#include <string.h>
#include <assert.h> // FIX #4: Для отладочных проверок

/*
 * Low-Level Driver Implementation.
 * Реализация драйвера управления аппаратным обеспечением.
 *
 * Основные механизмы:
 * - Мультиплексирование через повторяющийся таймер (repeating_timer).
 * - Регулировка яркости (PWM) через аппаратный будильник (alarm).
 * - Программная эмуляция SPI (Bit-banging).
 *
 * Модель синхронизации (Issue #10):
 * - Спинлоки удалены как избыточные.
 * - Volatile убраны для упрощения.
 * - Одиночные записи (uint8_t/vfd_seg_t) считаются атомарными.
 * - Критические секции используются только для атомарного чтения пары значений в ISR
 *   и при массовом обновлении яркости.
 */

// ============================================================================
//  КОНФИГУРАЦИЯ
// ============================================================================

#define LL_SHIFT_DELAY() __asm volatile ("nop\n nop\n nop\n");
#define LL_MIN_PULSE_US   4

// ============================================================================
//  ВНУТРЕННЕЕ СОСТОЯНИЕ
// ============================================================================

typedef struct
{
    // Флаги состояния
    bool initialized;
    bool refresh_running;

    // Конфигурация GPIO
    uint8_t data_pin;
    uint8_t clock_pin;
    uint8_t latch_pin;

    // Параметры дисплея
    uint8_t  digit_count;
    uint16_t refresh_rate_hz;
    uint32_t slot_period_us;

    // Буферы данных
    vfd_segment_map_t seg_buffer[VFD_MAX_DIGITS];
    uint8_t           brightness[VFD_MAX_DIGITS];

    // Контекст развертки
    uint8_t current_digit;
    bool extended_grid_mode; 

    struct repeating_timer fast_timer;
    alarm_id_t clear_alarm;

    bool gamma_enabled;

} display_ll_state_t;

static display_ll_state_t s_ll;

// ============================================================================
//  BIT-BANGING (SPI EMULATION)
// ============================================================================

/* Программная отправка байта (MSB first). */
static inline void ll_shift_byte(uint8_t data)
{
    for (int i = 7; i >= 0; i--)
    {
        gpio_put(s_ll.data_pin, (data >> i) & 1u);
        gpio_put(s_ll.clock_pin, 1);
        LL_SHIFT_DELAY();
        gpio_put(s_ll.clock_pin, 0);
    }
}

/* Защелкивание данных (Latch pulse). */
static inline void ll_latch(void)
{
    gpio_put(s_ll.latch_pin, 1);
    LL_SHIFT_DELAY();
    gpio_put(s_ll.latch_pin, 0);
    LL_SHIFT_DELAY();
}

/* 
 * Отправка кадра данных.
 * Порядок вывода: [Grid Low] -> (Grid High) -> [Segments]
 */
static inline void ll_shift_frame(uint16_t grid_data, uint8_t segs)
{
    // 1. Младший байт сетки (Grids 0-7)
    ll_shift_byte((uint8_t)(grid_data & 0xFFu));
    
    // 2. Старший байт сетки (Grids 8-15)
    if (s_ll.extended_grid_mode) {
        ll_shift_byte((uint8_t)((grid_data >> 8) & 0xFFu));
    }

    // 3. Сегменты
    ll_shift_byte(segs);

    ll_latch();
}

// ============================================================================
//  ГАММА-КОРРЕКЦИЯ
// ============================================================================

/* 
 * Вычисление квадратичной гаммы.
 */
static inline uint8_t ll_gamma_calc(uint8_t x)
{
    if (x == 0) return 0;
    if (x == 255) return 255;

    // Расчет для промежуточных значений (x^2 аппроксимация)
    uint32_t v = (uint32_t)x * x + 254u;
    v /= 255u;
    return (uint8_t)v;
}

// ============================================================================
//  ОБРАБОТЧИКИ ПРЕРЫВАНИЙ (IRQ)
// ============================================================================

/*
 * Callback аппаратного будильника (PWM OFF).
 * Гасит дисплей (отправляет нули), соблюдая разрядность шины.
 */
static int64_t ll_clear_cb(alarm_id_t id, void *user_data)
{
    (void)id;
    (void)user_data;
    // Отправляем пустой кадр (0 для сеток, 0 для сегментов)
    ll_shift_frame(0x0000, 0x00);
    return 0;
}

/*
 * Callback повторяющегося таймера (Multiplexing Step).
 */
static bool ll_fast_timer_cb(struct repeating_timer *t)
{
    (void)t;

    if (!s_ll.initialized) return true;

    uint8_t digit = s_ll.current_digit;
    if (digit >= s_ll.digit_count) digit = 0;

    // Атомарное чтение данных для текущего разряда
    uint32_t irq = save_and_disable_interrupts();
    vfd_segment_map_t segs = s_ll.seg_buffer[digit];
    uint8_t           pwm  = s_ll.brightness[digit];
    restore_interrupts(irq);

    uint16_t grid_pattern = (uint16_t)(1u << digit);

    // Вывод данных
    ll_shift_frame(grid_pattern, segs);

    // Логика PWM
    if (pwm == 0)
    {
        ll_shift_frame(0x0000, 0x00);
    }
    else if (pwm < 255)
    {
        if (s_ll.clear_alarm >= 0) {
            cancel_alarm(s_ll.clear_alarm);
            s_ll.clear_alarm = -1;
        }

        uint32_t on_us = ((uint32_t)pwm * s_ll.slot_period_us) >> 8;
        uint32_t max_safe_us = s_ll.slot_period_us > 10 ? s_ll.slot_period_us - 10 : s_ll.slot_period_us;
        
        if (on_us > max_safe_us) on_us = max_safe_us;
        if (on_us < LL_MIN_PULSE_US) on_us = LL_MIN_PULSE_US;

        alarm_id_t new_id = add_alarm_in_us((int64_t)on_us, ll_clear_cb, NULL, true);
        if (new_id >= 0) s_ll.clear_alarm = new_id;
        else {
            ll_shift_frame(0x0000, 0x00);
            s_ll.clear_alarm = -1;
        }
    }

    digit++;
    if (digit >= s_ll.digit_count) digit = 0;
    s_ll.current_digit = digit;

    return true;
}

// ============================================================================
//  ИНИЦИАЛИЗАЦИЯ И УПРАВЛЕНИЕ
// ============================================================================

bool display_ll_init(const display_ll_config_t *cfg)
{
    if (!cfg) return false;
    if (cfg->digit_count == 0 || cfg->digit_count > VFD_MAX_DIGITS) return false;
    if (cfg->refresh_rate_hz < 50 || cfg->refresh_rate_hz > 2000) return false;

    if (s_ll.initialized) display_ll_deinit();

    gpio_init(cfg->data_pin);
    gpio_init(cfg->clock_pin);
    gpio_init(cfg->latch_pin);

    gpio_set_dir(cfg->data_pin,  GPIO_OUT);
    gpio_set_dir(cfg->clock_pin, GPIO_OUT);
    gpio_set_dir(cfg->latch_pin, GPIO_OUT);
    
    gpio_set_slew_rate(cfg->data_pin, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(cfg->clock_pin, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(cfg->latch_pin, GPIO_SLEW_RATE_FAST);

    gpio_put(cfg->data_pin, 0);
    gpio_put(cfg->clock_pin, 0);
    gpio_put(cfg->latch_pin, 0);

    memset(&s_ll, 0, sizeof(s_ll));

    // SYNC: Удалена инициализация Spinlock (Issue #10)

    s_ll.data_pin        = cfg->data_pin;
    s_ll.clock_pin       = cfg->clock_pin;
    s_ll.latch_pin       = cfg->latch_pin;
    s_ll.digit_count     = cfg->digit_count;
    s_ll.refresh_rate_hz = cfg->refresh_rate_hz;
    s_ll.gamma_enabled   = true;

    // Определяем режим работы шины
    s_ll.extended_grid_mode = (cfg->digit_count > 8);

    for (int i = 0; i < VFD_MAX_DIGITS; i++) {
        s_ll.seg_buffer[i] = 0;
        s_ll.brightness[i] = 255;
    }

    s_ll.clear_alarm = -1;
    s_ll.initialized = true;
    return true;
}

bool display_ll_is_initialized(void) { return s_ll.initialized; }

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
    s_ll.clear_alarm    = -1;
    s_ll.refresh_running = true;

    if (!add_repeating_timer_us(period_us, ll_fast_timer_cb, NULL, &s_ll.fast_timer)) {
        s_ll.refresh_running = false;
        return false;
    }
    return true;
}

void display_ll_stop_refresh(void)
{
    if (!s_ll.initialized || !s_ll.refresh_running) return;
    
    cancel_repeating_timer(&s_ll.fast_timer);
    
    if (s_ll.clear_alarm >= 0) {
        cancel_alarm(s_ll.clear_alarm);
        s_ll.clear_alarm = -1;
    }
    
    ll_shift_frame(0x0000, 0x00);
    
    s_ll.refresh_running = false;
}

void display_ll_deinit(void)
{
    if (!s_ll.initialized) return;
    display_ll_stop_refresh();
    
    // SYNC: Удалено освобождение spinlock
    
    gpio_set_dir(s_ll.data_pin,  GPIO_IN);
    gpio_set_dir(s_ll.clock_pin, GPIO_IN);
    gpio_set_dir(s_ll.latch_pin, GPIO_IN);
    
    memset(&s_ll, 0, sizeof(s_ll));
    s_ll.clear_alarm = -1;
}

// ============================================================================
//  API ДОСТУПА К БУФЕРАМ
// ============================================================================

uint8_t display_ll_get_digit_count(void) { return s_ll.digit_count; }
vfd_segment_map_t *display_ll_get_buffer(void) { return s_ll.seg_buffer; }

void display_ll_set_digit_raw(uint8_t idx, vfd_segment_map_t segments)
{
    if (!s_ll.initialized) return;

    // FIX #4: Assert срабатывает только в Debug-сборке, помогая найти ошибку.
    // В Release-сборке assert вырезается компилятором.
    assert(idx < s_ll.digit_count);

    // Защита времени выполнения (Runtime Safety) для Release-сборки.
    // Если индекс неверен, мы просто игнорируем запись, чтобы не повредить память.
    if (idx >= s_ll.digit_count) return;
    
    s_ll.seg_buffer[idx] = segments;
}

void display_ll_set_brightness(uint8_t idx, uint8_t lvl)
{
    if (!s_ll.initialized) return;

    // FIX #4: Отладочная проверка диапазона
    assert(idx < s_ll.digit_count);

    // Runtime защита
    if (idx >= s_ll.digit_count) return;
    
    s_ll.brightness[idx] = lvl;
}

void display_ll_set_brightness_all(uint8_t lvl)
{
    if (!s_ll.initialized) return;
    
    // Здесь assert не нужен, так как мы итерируемся по внутреннему limit
    uint32_t irq = save_and_disable_interrupts();
    for (int i = 0; i < s_ll.digit_count; i++)
        s_ll.brightness[i] = lvl;
    restore_interrupts(irq);
}

void display_ll_enable_gamma(bool en) { s_ll.gamma_enabled = en; }
uint8_t display_ll_apply_gamma(uint8_t x) { return s_ll.gamma_enabled ? ll_gamma_calc(x) : x; }