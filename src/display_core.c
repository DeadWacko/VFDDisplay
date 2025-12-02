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
 * FIX #23: Полная переработка логики точек (Static vs Blink).
 */

static display_state_t g_display_state;
display_state_t *const g_display = &g_display_state;

/* Конфигурация по умолчанию */
#define DISPLAY_DEFAULT_DATA_PIN      15
#define DISPLAY_DEFAULT_CLOCK_PIN     14
#define DISPLAY_DEFAULT_LATCH_PIN     13
#define DISPLAY_DEFAULT_REFRESH_HZ    120
#define DISPLAY_DEFAULT_ADC_PIN       26   

#define DISPLAY_BRIGHTNESS_MIN_AUTO   5    
#define DISPLAY_BRIGHTNESS_NIGHT      10   
#define DISPLAY_BRIGHTNESS_UPDATE_MS  1000 
#define DISPLAY_BRIGHTNESS_HYSTERESIS 2    

#define DOT_DEFAULT_PERIOD_MS         1000 
#define DOT_DEFAULT_BIT               7    
// По умолчанию точка на индексе 1 (для 4-значных часов это XX.XX)
#define DOT_DEFAULT_MASK              (1 << 1) 

// ... (Вспомогательные функции блокировки FX - core_is_fx_segment_blocking и т.д. оставляем как в v1.1.1) ...

static bool core_is_fx_segment_blocking(void) {
    if (!g_display->fx_active) return false;
    switch (g_display->fx_type) {
        case FX_GLITCH: case FX_MORPH: case FX_DISSOLVE:
        case FX_MARQUEE: case FX_SLIDE_IN: case FX_SLOT_MACHINE:
        case FX_DECODE: case FX_PINGPONG: return true;
        default: return false;
    }
}

static bool core_does_fx_control_brightness(void) {
    if (!g_display->fx_active) return false;
    switch (g_display->fx_type) {
        case FX_FADE_IN: case FX_FADE_OUT: case FX_PULSE:
        case FX_WAVE: case FX_MATRIX: case FX_HEARTBEAT: return true;
        default: return false;
    }
}

static void core_push_brightness_to_ll(uint8_t level) {
    if (!display_ll_is_initialized()) return;
    display_ll_set_brightness_all(level);
}

/* 
 * FIX #23: Новая логика наложения точек.
 * Теперь точки накладываются, если установлен бит в dot_map.
 * Режим blinking определяет, мигает точка или горит постоянно.
 */
static void core_push_content_to_ll(void)
{
    if (!display_ll_is_initialized() || !g_display->initialized) return;

    uint8_t digits = g_display->digit_count;
    if (digits > VFD_MAX_DIGITS) digits = VFD_MAX_DIGITS;

    for (uint8_t i = 0; i < digits; i++) {
        vfd_segment_map_t seg = g_display->content_buffer[i];
        
        // Проверяем, должна ли быть точка на этом разряде
        if (g_display->dot_map & (1u << i)) {
            bool draw_dot = false;

            if (g_display->dot_blink_enabled) {
                // Режим мигания: рисуем только в фазе ON
                if (g_display->dot_state) draw_dot = true;
            } else {
                // Режим статики: рисуем всегда
                draw_dot = true;
            }

            if (draw_dot) {
                seg |= (vfd_segment_map_t)(1u << g_display->dot_bit);
            }
        }
        
        display_ll_set_digit_raw(i, seg);
    }
}

// ... (Функции core_read_adc_filtered, core_update_brightness_now, core_brightness_tick - без изменений) ...

static uint16_t core_read_adc_filtered(uint16_t adc_pin) {
    uint16_t input = 0;
    if (adc_pin >= 26 && adc_pin <= 29) input = (uint16_t)(adc_pin - 26);
    adc_select_input(input);
    uint32_t acc = 0;
    for (uint8_t i = 0; i < 8; i++) acc += adc_read();
    return (uint16_t)(acc / 8);
}

static void core_update_brightness_now(void) {
    uint8_t new_level = g_display->user_brightness_level;
    if (g_display->auto_brightness_enabled) {
        uint16_t raw = core_read_adc_filtered((uint16_t)g_display->adc_pin); 
        uint8_t level = (uint8_t)((uint32_t)raw * VFD_MAX_BRIGHTNESS / 4095u);
        if (level < DISPLAY_BRIGHTNESS_MIN_AUTO) level = DISPLAY_BRIGHTNESS_MIN_AUTO;
        new_level = level;
    } else if (g_display->night_mode_enabled && rtc_running()) {
        datetime_t dt; rtc_get_datetime(&dt);
        uint8_t start = g_display->night_start_hour;
        uint8_t end = g_display->night_end_hour;
        bool is_night = (start < end) ? (dt.hour >= start && dt.hour < end) : (dt.hour >= start || dt.hour < end);
        if (is_night) new_level = g_display->night_brightness;
    }
    if (new_level > VFD_MAX_BRIGHTNESS) new_level = VFD_MAX_BRIGHTNESS;

    if (g_display->fx_active) g_display->fx_base_brightness = new_level;
    if (core_does_fx_control_brightness()) return;

    uint8_t current = g_display->final_brightness[0];
    uint8_t diff = (current > new_level) ? (current - new_level) : (new_level - current);
    if (diff < DISPLAY_BRIGHTNESS_HYSTERESIS && !g_display->fx_active) return;

    for (uint8_t i = 0; i < g_display->digit_count; i++) g_display->final_brightness[i] = new_level;
    core_push_brightness_to_ll(new_level);
}

static void core_brightness_tick(absolute_time_t now) {
    if (g_display->ov_active) return;
    if (!(g_display->auto_brightness_enabled || g_display->night_mode_enabled)) return;
    if (to_ms_since_boot(now) - to_ms_since_boot(g_display->brightness_last_update) < g_display->brightness_update_period_ms) return;
    g_display->brightness_last_update = now;
    core_update_brightness_now();
}

static void core_dot_blink_tick(absolute_time_t now)
{
    // Тик таймера нужен только если включено мигание
    if (!g_display->dot_blink_enabled || g_display->digit_count == 0) return;
    if (g_display->ov_active) return;
    
    // Если эффектов нет или они не блокируют сегменты
    if (core_is_fx_segment_blocking()) return;

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
    
    // FIX #23: Инициализация карты точек
    g_display->dot_map = DOT_DEFAULT_MASK; 
    g_display->dot_blink_enabled = true; // По умолчанию мигает
    g_display->dot_bit = DOT_DEFAULT_BIT;

    for(int i=0; i<VFD_MAX_DIGITS; i++) {
        g_display->content_brightness[i] = VFD_MAX_BRIGHTNESS;
        g_display->final_brightness[i] = VFD_MAX_BRIGHTNESS;
    }

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

display_mode_t display_get_mode(void) {
    if (g_display->ov_active) return DISPLAY_MODE_OVERLAY;
    if (g_display->fx_active) return DISPLAY_MODE_EFFECT;
    return DISPLAY_MODE_CONTENT;
}

extern bool display_fx_is_running(void);
extern bool display_is_overlay_running(void);
bool display_is_effect_running(void) { return display_fx_is_running(); }

void display_set_brightness(uint8_t brightness) {
    if (brightness > VFD_MAX_BRIGHTNESS) brightness = VFD_MAX_BRIGHTNESS;
    g_display->user_brightness_level = brightness;
    
    if (!g_display->auto_brightness_enabled && !g_display->night_mode_enabled) {
        if (!g_display->fx_active || !core_does_fx_control_brightness()) {
             for (uint8_t i = 0; i < g_display->digit_count; i++) 
                g_display->final_brightness[i] = brightness;
            core_push_brightness_to_ll(brightness);
        }
        if (g_display->fx_active) g_display->fx_base_brightness = brightness;
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

/* FIX #23: Управление точками */

void display_set_dot_blinking(bool enable) {
    g_display->dot_blink_enabled = enable;
    // Сбрасываем таймер и состояние для синхронизации
    g_display->dot_state = false; 
    g_display->dot_last_toggle = get_absolute_time();
    core_push_content_to_ll();
}

void display_set_dots_config(uint16_t mask, bool blink) {
    g_display->dot_map = mask;
    display_set_dot_blinking(blink);
}

void display_core_set_buffer(const vfd_segment_map_t *buf, uint8_t size) {
    if (!g_display->initialized || !buf) return;
    uint8_t n = size > g_display->digit_count ? g_display->digit_count : size;
    for (uint8_t i = 0; i < n; i++) g_display->content_buffer[i] = buf[i];
    for (uint8_t i = n; i < g_display->digit_count; i++) g_display->content_buffer[i] = 0;

    if (!display_is_overlay_running()) {
        if (!core_is_fx_segment_blocking()) {
            core_push_content_to_ll();
        }
    }
}

vfd_segment_map_t *display_content_buffer(void) { return g_display->content_buffer; }

extern void display_fx_tick(void);
extern void display_overlay_tick(void);

void display_process(void) {
    if (!g_display->initialized) return;
    absolute_time_t now = get_absolute_time();
    core_brightness_tick(now);

    if (display_is_overlay_running()) {
        g_display->ov_active = true;
        g_display->fx_active = false;
        display_overlay_tick();
        g_display->mode = DISPLAY_MODE_OVERLAY;
        return;
    } else {
        g_display->ov_active = false;
    }

    if (display_fx_is_running()) {
        g_display->fx_active = true;
        display_fx_tick();
        if (core_is_fx_segment_blocking()) {
            g_display->mode = DISPLAY_MODE_EFFECT;
            return; 
        } 
        g_display->mode = DISPLAY_MODE_CONTENT;
    } else {
        g_display->fx_active = false;
        g_display->mode = DISPLAY_MODE_CONTENT;
    }

    core_dot_blink_tick(now);
    core_push_content_to_ll(); // Обновляет контент + точки (Static/Blink)
}