#include "display_api.h"
#include "display_ll.h"
#include "display_state.h"
#include "logging.h"

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/rtc.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * High-Level Core.
 * Реализация центральной логики дисплея:
 * - Хранение глобального состояния.
 * - Управление приоритетами (Overlay -> Effect -> Content).
 * - Маршрутизация данных в драйвер низкого уровня (LL).
 * - Системные функции (автояркость, мигание точек).
 */

static display_state_t g_display_state;
display_state_t *const g_display = &g_display_state;

/* Конфигурация оборудования по умолчанию */
#define DISPLAY_DEFAULT_DATA_PIN      15
#define DISPLAY_DEFAULT_CLOCK_PIN     14
#define DISPLAY_DEFAULT_LATCH_PIN     13
#define DISPLAY_DEFAULT_REFRESH_HZ    120
#define DISPLAY_DEFAULT_ADC_PIN       26   

/* Параметры автоматической яркости */
#define DISPLAY_BRIGHTNESS_MIN_AUTO   5    
#define DISPLAY_BRIGHTNESS_NIGHT      10   
#define DISPLAY_BRIGHTNESS_UPDATE_MS  1000 
#define DISPLAY_BRIGHTNESS_HYSTERESIS 2    

/* Параметры мигания точек */
#define DOT_DEFAULT_PERIOD_MS         1000 
#define DOT_DEFAULT_BIT               7    
#define DOT_DEFAULT_POS1              1    
#define DOT_DEFAULT_POS2              2    

// ============================================================================
//  Вспомогательные функции
// ============================================================================

/*
 * Проверка типа текущего эффекта.
 * Возвращает true, если эффект является блокирующим (захватывает управление сегментами).
 * Возвращает false, если эффект прозрачный (управляет только яркостью).
 */
static bool core_is_fx_blocking(void)
{
    if (!g_display->fx_active) return false;
    
    switch (g_display->fx_type) {
        case FX_GLITCH:
        case FX_MORPH:
        case FX_DISSOLVE:
        case FX_MARQUEE:
        case FX_SLIDE_IN:
        case FX_SLOT_MACHINE:
        case FX_DECODE:
        case FX_PINGPONG:
            return true;
            
        case FX_FADE_IN:
        case FX_FADE_OUT:
        case FX_PULSE:
        case FX_WAVE:
        case FX_MATRIX:
        case FX_HEARTBEAT:
        default:
            return false;
    }
}

static inline uint8_t clamp_u8(uint32_t v, uint8_t max_val)
{
    if (v > max_val) return max_val;
    return (uint8_t)v;
}

/* Отправка глобального уровня яркости в драйвер LL. */
static void core_push_brightness_to_ll(uint8_t level)
{
    if (!display_ll_is_initialized()) return;
    display_ll_set_brightness_all(level);
}

/*
 * Слияние буфера контента с состоянием точек и отправка в LL.
 * Выполняется только если управление не захвачено блокирующим эффектом.
 */
static void core_push_content_to_ll(void)
{
    if (!display_ll_is_initialized() || !g_display->initialized) return;

    uint8_t digits = g_display->digit_count;
    if (digits > VFD_MAX_DIGITS) digits = VFD_MAX_DIGITS;

    for (uint8_t i = 0; i < digits; i++) {
        vfd_seg_t seg = g_display->content_buffer[i];
        
        // Наложение маски разделительных точек (если включено)
        if (g_display->dot_blink_enabled && g_display->dot_state) {
            for (uint8_t k = 0; k < 2; k++) {
                if (g_display->dot_digit_positions[k] == i) {
                    seg |= (vfd_seg_t)(1u << g_display->dot_bit);
                }
            }
        }
        display_ll_set_digit_raw(i, seg);
    }
}

/* Чтение АЦП с усреднением для подавления шума. */
static uint16_t core_read_adc_filtered(uint16_t adc_pin)
{
    uint16_t input = 0;
    if (adc_pin >= 26 && adc_pin <= 29) input = (uint16_t)(adc_pin - 26);
    adc_select_input(input);
    const uint8_t samples = 8;
    uint32_t acc = 0;
    for (uint8_t i = 0; i < samples; i++) acc += adc_read();
    return (uint16_t)(acc / samples);
}

/* Расчет и применение целевой яркости (Manual / Auto / Night). */
static void core_update_brightness_now(void)
{
    uint8_t new_level = g_display->user_brightness_level;

    if (g_display->auto_brightness_enabled) {
        // Расчет по датчику освещенности
        uint16_t raw = core_read_adc_filtered((uint16_t)g_display->adc_pin); 
        uint32_t val = (uint32_t)raw * VFD_MAX_BRIGHTNESS;
        uint8_t level = (uint8_t)(val / 4095u);
        if (level < DISPLAY_BRIGHTNESS_MIN_AUTO) level = DISPLAY_BRIGHTNESS_MIN_AUTO;
        new_level = level;
    } else if (g_display->night_mode_enabled && rtc_running()) {
        // Расчет по времени суток (RTC)
        datetime_t dt;
        rtc_get_datetime(&dt);
        bool is_night = false;
        uint8_t start = g_display->night_start_hour;
        uint8_t end   = g_display->night_end_hour;
        
        if (start < end) is_night = (dt.hour >= start && dt.hour < end);
        else is_night = (dt.hour >= start || dt.hour < end);
        
        if (is_night) new_level = g_display->night_brightness;
    }

    if (new_level > VFD_MAX_BRIGHTNESS) new_level = VFD_MAX_BRIGHTNESS;

    // Применение с гистерезисом для предотвращения мерцания
    uint8_t current = g_display->final_brightness[0];
    uint8_t diff = (current > new_level) ? (current - new_level) : (new_level - current);
    if (diff < DISPLAY_BRIGHTNESS_HYSTERESIS) return;

    for (uint8_t i = 0; i < g_display->digit_count; i++) g_display->final_brightness[i] = new_level;
    core_push_brightness_to_ll(new_level);
}

/* Периодическое обновление яркости (Heartbeat). */
static void core_brightness_tick(absolute_time_t now)
{
    // Блокировка автояркости во время активных анимаций
    if (g_display->fx_active || g_display->ov_active) return;

    if (!(g_display->auto_brightness_enabled || g_display->night_mode_enabled)) return;

    uint32_t now_ms  = to_ms_since_boot(now);
    uint32_t last_ms = to_ms_since_boot(g_display->brightness_last_update);
    if (now_ms - last_ms < g_display->brightness_update_period_ms) return;

    g_display->brightness_last_update = now;
    core_update_brightness_now();
}

/* Логика мигания разделительных точек. */
static void core_dot_blink_tick(absolute_time_t now)
{
    if (!g_display->dot_blink_enabled || g_display->digit_count == 0) return;
    if (g_display->ov_active) return;
    
    // Блокирующие эффекты перехватывают контроль над точками
    if (core_is_fx_blocking()) return;

    uint32_t now_ms  = to_ms_since_boot(now);
    uint32_t last_ms = to_ms_since_boot(g_display->dot_last_toggle);
    if (now_ms - last_ms < g_display->dot_period_ms) return;

    g_display->dot_last_toggle = now;
    g_display->dot_state = !g_display->dot_state;
    core_push_content_to_ll();
}

// ============================================================================
//  Реализация API
// ============================================================================

void display_init(uint8_t digit_count)
{
    if (digit_count == 0 || digit_count > VFD_MAX_DIGITS) digit_count = 4;
    memset(&g_display_state, 0, sizeof(g_display_state));

    // Настройка состояния по умолчанию
    g_display->digit_count = digit_count;
    g_display->refresh_rate_hz = DISPLAY_DEFAULT_REFRESH_HZ;
    g_display->user_brightness_level = VFD_MAX_BRIGHTNESS;
    g_display->night_brightness = DISPLAY_BRIGHTNESS_NIGHT;
    g_display->night_start_hour = 23;
    g_display->night_end_hour = 7;
    g_display->adc_pin = DISPLAY_DEFAULT_ADC_PIN;
    g_display->brightness_update_period_ms = DISPLAY_BRIGHTNESS_UPDATE_MS;
    g_display->brightness_last_update = get_absolute_time();
    g_display->dot_period_ms = DOT_DEFAULT_PERIOD_MS;
    g_display->dot_last_toggle = get_absolute_time();
    g_display->dot_digit_positions[0] = DOT_DEFAULT_POS1;
    g_display->dot_digit_positions[1] = DOT_DEFAULT_POS2;
    g_display->dot_bit = DOT_DEFAULT_BIT;

    for(int i=0; i<VFD_MAX_DIGITS; i++) {
        g_display->content_brightness[i] = VFD_MAX_BRIGHTNESS;
        g_display->final_brightness[i] = VFD_MAX_BRIGHTNESS;
    }

    // Инициализация драйвера низкого уровня
    display_ll_config_t cfg = {
        .data_pin = DISPLAY_DEFAULT_DATA_PIN,
        .clock_pin = DISPLAY_DEFAULT_CLOCK_PIN,
        .latch_pin = DISPLAY_DEFAULT_LATCH_PIN,
        .digit_count = g_display->digit_count,
        .refresh_rate_hz = g_display->refresh_rate_hz,
    };

    if (!display_ll_init(&cfg)) {
        LOG_ERROR("display_init: LL init failed");
        return;
    }
    display_ll_start_refresh();

    // Инициализация периферии
    adc_init();
    adc_gpio_init((uint)g_display->adc_pin);
    if (g_display->adc_pin >= 26 && g_display->adc_pin <= 29) {
        adc_select_input(g_display->adc_pin - 26);
    }

    core_update_brightness_now();
    core_push_content_to_ll();
    g_display->initialized = true;
    LOG_INFO("display_init: Success");
}

display_mode_t display_get_mode(void)
{
    if (g_display->ov_active) return DISPLAY_MODE_OVERLAY;
    if (g_display->fx_active) return DISPLAY_MODE_EFFECT;
    return DISPLAY_MODE_CONTENT;
}

extern bool display_fx_is_running(void);
extern bool display_is_overlay_running(void);
bool display_is_effect_running(void) { return display_fx_is_running(); }

void display_set_brightness(uint8_t brightness)
{
    if (brightness > VFD_MAX_BRIGHTNESS) brightness = VFD_MAX_BRIGHTNESS;
    g_display->user_brightness_level = brightness;
    
    // Мгновенное применение, если не активны автоматические режимы или эффекты
    if (!g_display->auto_brightness_enabled && !g_display->night_mode_enabled && !g_display->fx_active) {
        for (uint8_t i = 0; i < g_display->digit_count; i++) 
            g_display->final_brightness[i] = brightness;
        core_push_brightness_to_ll(brightness);
    } else {
        core_update_brightness_now();
    }
}

void display_set_auto_brightness(bool enable) {
    g_display->auto_brightness_enabled = enable;
    if (enable) g_display->night_mode_enabled = false;
    core_update_brightness_now();
}

void display_set_night_mode(bool enable) {
    g_display->night_mode_enabled = enable;
    if (enable) g_display->auto_brightness_enabled = false;
    core_update_brightness_now();
}

void display_set_dot_blinking(bool enable) {
    g_display->dot_blink_enabled = enable;
    g_display->dot_state = false;
    g_display->dot_last_toggle = get_absolute_time();
    if (!enable) core_push_content_to_ll();
}

void display_core_set_buffer(const vfd_seg_t *buf, uint8_t size)
{
    if (!g_display->initialized || !buf) return;

    uint8_t n = size > g_display->digit_count ? g_display->digit_count : size;
    
    // Копирование данных в основной буфер
    for (uint8_t i = 0; i < n; i++) g_display->content_buffer[i] = buf[i];
    for (uint8_t i = n; i < g_display->digit_count; i++) g_display->content_buffer[i] = 0;

    // Обновление дисплея, если не активны оверлеи или блокирующие эффекты
    if (!display_is_overlay_running()) {
        if (!core_is_fx_blocking()) {
            core_push_content_to_ll();
        }
    }
}

vfd_seg_t *display_content_buffer(void) { return g_display->content_buffer; }

extern void display_fx_tick(void);
extern void display_overlay_tick(void);

/*
 * Главный цикл обработки.
 * Управляет жизненным циклом кадров, эффектов и приоритетами.
 */
void display_process(void)
{
    if (!g_display->initialized) return;
    absolute_time_t now = get_absolute_time();

    // 1. Автоматическое управление яркостью (если нет эффектов)
    core_brightness_tick(now);

    // 2. Обработка оверлеев (Высший приоритет)
    if (display_is_overlay_running()) {
        g_display->ov_active = true;
        g_display->fx_active = false;
        display_overlay_tick();
        g_display->mode = DISPLAY_MODE_OVERLAY;
        return;
    } else {
        g_display->ov_active = false;
    }

    // 3. Обработка эффектов
    if (display_fx_is_running()) {
        g_display->fx_active = true;
        display_fx_tick();
        
        // Если эффект блокирующий (структурный), прерываем обновление контента
        if (core_is_fx_blocking()) {
            g_display->mode = DISPLAY_MODE_EFFECT;
            return; 
        } 
        // Если эффект прозрачный (яркостный), продолжаем обновление
        g_display->mode = DISPLAY_MODE_CONTENT;
    } else {
        g_display->fx_active = false;
        g_display->mode = DISPLAY_MODE_CONTENT;
    }

    // 4. Обновление контента и точек
    core_dot_blink_tick(now);
    core_push_content_to_ll();
}