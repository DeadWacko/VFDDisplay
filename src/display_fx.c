#include "display_api.h"
#include "display_ll.h"
#include "display_state.h"
#include "display_rng.h"
#include "display_lut.h" 
#include "display_font.h"
#include "logging.h"

#include "pico/stdlib.h"
#include <string.h>

/*
 * FX Engine.
 * Модуль реализации процедурных анимаций.
 * Управляет изменением яркости и содержимого буферов во времени.
 */

static bool s_rng_seeded = false;

// ============================================================================
//   ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================================

/* Расчет времени, прошедшего с момента start (в миллисекундах). */
static inline uint32_t fx_elapsed_ms(absolute_time_t from, absolute_time_t to) {
    int64_t diff_us = absolute_time_diff_us(from, to);
    if (diff_us <= 0) return 0;
    return (uint32_t)(diff_us / 1000u);
}

/* Инициализация генератора случайных чисел (если еще не выполнена). */
static void fx_seed_rng_if_needed(void) {
    if (s_rng_seeded) return;
    display_rng_seed_from_adc((uint16_t)g_display->adc_pin);
    s_rng_seeded = true;
}

/*
 * Определение типа эффекта.
 * Возвращает true, если эффект требует эксклюзивного контроля над сегментами.
 */
static bool fx_is_blocking_type(fx_type_t type) {
    switch (type) {
        case FX_GLITCH:
        case FX_MORPH:
        case FX_DISSOLVE:
        case FX_MARQUEE:
        case FX_SLIDE_IN:
            return true;
        default: 
            return false; // Прозрачные эффекты (яркость)
    }
}

/*
 * Завершение эффекта.
 * Восстанавливает состояние дисплея (яркость и/или контент) из Snapshot.
 *
 * FIX #15: Для Morph подменяем snapshot на target, чтобы "зафиксировать" результат.
 */
static void fx_finish_internal(void)
{
    if (!g_display->fx_active) return;
    fx_type_t finished_type = g_display->fx_type;
    bool was_blocking = fx_is_blocking_type(finished_type);

    // Специальная обработка для Morph
    if (finished_type == FX_MORPH && g_display->saved_valid) {
        uint8_t digits = g_display->digit_count;
        // 1. Обновляем основной буфер контента (чтобы логика Core знала о новом состоянии)
        memcpy(g_display->content_buffer, g_display->fx_morph_target, digits);
        
        // 2. Обновляем Snapshot (чтобы блок восстановления ниже записал target в LL)
        memcpy(g_display->saved_content_buffer, g_display->fx_morph_target, digits);
    }

    if (was_blocking && g_display->saved_valid) {
        // Для блокирующих эффектов восстанавливаем состояние из snapshot.
        // В случае Morph snapshot уже содержит новые данные (target).
        uint8_t digits = g_display->digit_count;
        for (uint8_t i = 0; i < digits; i++) {
            display_ll_set_digit_raw(i, g_display->saved_content_buffer[i]);
            display_ll_set_brightness(i, g_display->saved_brightness[i]);
            g_display->final_brightness[i] = g_display->saved_brightness[i];
        }
    } else {
        // Для прозрачных эффектов: восстановление только базовой яркости
        uint8_t restore_level = g_display->fx_base_brightness;
        if (g_display->saved_valid) restore_level = g_display->saved_brightness[0];
        
        display_ll_set_brightness_all(restore_level);
        for (uint8_t i = 0; i < g_display->digit_count; i++) 
            g_display->final_brightness[i] = restore_level;
    }

    g_display->saved_valid = false;
    g_display->fx_active = false;
    g_display->fx_type   = FX_NONE;

    if (g_display->on_effect_finished) g_display->on_effect_finished(finished_type);
}

/*
 * Базовая инициализация любого эффекта.
 * Создает Snapshot текущего состояния и настраивает таймеры.
 */
static bool fx_start_basic(fx_type_t type, uint32_t duration_ms, uint32_t frame_ms)
{
    if (!g_display->initialized) return false;
    if (g_display->ov_active) return false;
    if (g_display->fx_active) return false;
    if (duration_ms == 0) return false;

    fx_seed_rng_if_needed();

    uint8_t digits = g_display->digit_count;
    for (uint8_t i = 0; i < digits; i++) {
        g_display->saved_content_buffer[i] = g_display->content_buffer[i];
        g_display->saved_brightness[i]     = g_display->final_brightness[i];
    }
    g_display->saved_valid = true;

    g_display->fx_active = true;
    g_display->fx_type = type;
    g_display->fx_start_time = get_absolute_time();
    g_display->fx_duration_ms = duration_ms;
    g_display->fx_frame_ms = frame_ms;
    g_display->fx_elapsed_ms = 0;
    
    uint8_t base = g_display->final_brightness[0];
    if (base == 0) base = VFD_MAX_BRIGHTNESS;
    g_display->fx_base_brightness = base;

    // Сброс внутренних счетчиков эффектов
    g_display->fx_glitch_active = false;
    
    // FIX #16: Сбрасываем таймер следующего глюка, чтобы эффект начинался сразу,
    // а не ждал тайминга от предыдущего запуска.
    g_display->fx_glitch_next_ms = 0; 
    
    g_display->fx_matrix_last_ms = 0;
    g_display->fx_morph_step = 0;
    g_display->fx_dissolve_step = 0;
    return true;
}

/* Подготовка текстового буфера для эффектов Marquee/SlideIn. */
static void fx_prepare_text(const char *text) {
    if (!text) return;
    uint16_t len = 0;
    while (text[len] && len < (FX_TEXT_MAX_LEN - 1)) {
        g_display->fx_text_buffer[len] = text[len];
        len++;
    }
    g_display->fx_text_buffer[len] = '\0';
    g_display->fx_text_len = len;
}

// ============================================================================
//   РЕАЛИЗАЦИЯ ЭФФЕКТОВ
// ============================================================================

/* Fade In / Fade Out: Линейное изменение яркости с гамма-коррекцией. */
static void fx_apply_fade(uint32_t t_ms, uint32_t duration_ms, bool reverse) {
    if (duration_ms == 0) return;
    if (t_ms > duration_ms) t_ms = duration_ms;
    uint8_t base = g_display->fx_base_brightness;
    uint32_t num;
    if (!reverse) num = (uint32_t)t_ms * base;
    else          num = (uint32_t)(duration_ms - t_ms) * base;
    uint8_t linear = (uint8_t)(num / duration_ms);
    display_ll_set_brightness_all(display_ll_apply_gamma(linear));
}

/* Pulse: Синусоидальная модуляция яркости (дыхание). */
static void fx_apply_pulse(uint32_t t_ms, uint32_t duration_ms) {
    if (duration_ms == 0) return;
    uint8_t base = g_display->fx_base_brightness;
    const uint8_t min_percent = 8;
    const uint8_t span_percent = 100u - min_percent;
    const uint32_t cycles = 2;
    if (t_ms > duration_ms) t_ms = duration_ms;
    
    uint32_t phase_full = (uint32_t)((uint64_t)t_ms * 256u * cycles / duration_ms);
    uint8_t phase_idx = (uint8_t)(phase_full & 0xFFu);
    uint8_t cos_q = display_cos_lut[phase_idx];
    uint8_t dyn_percent = (uint8_t)((uint32_t)cos_q * span_percent / 255u);
    uint8_t brightness_percent = min_percent + dyn_percent;
    uint8_t linear = (uint8_t)((uint32_t)base * brightness_percent / 100u);
    display_ll_set_brightness_all(display_ll_apply_gamma(linear));
}

/* Вспомогательная кривая затухания для эффекта Wave. */
static uint8_t fx_ease_curve(uint32_t dist_ratio) {
    if (dist_ratio >= 255) return 0;
    uint32_t x = 255 - dist_ratio;
    return (uint8_t)((x * x) >> 8);
}

/* Wave: Волна яркости, бегущая по разрядам. */
static void fx_apply_wave(uint32_t t_ms, uint32_t duration_ms) {
    if (duration_ms == 0) return;
    uint8_t digits = g_display->digit_count;
    uint8_t base = g_display->fx_base_brightness;
    const uint8_t min_percent = 20;
    const uint8_t span_percent = 80;
    const uint32_t cycles = 2;
    if (t_ms > duration_ms) t_ms = duration_ms;

    uint32_t total_length = (uint32_t)digits * 256u;
    uint32_t wave_center = (uint32_t)((uint64_t)t_ms * cycles * total_length / duration_ms) % total_length;
    const uint32_t wave_radius = 384u;

    for (uint8_t i = 0; i < digits; i++) {
        uint32_t digit_center = (uint32_t)i * 256u + 128u;
        uint32_t dist = (digit_center > wave_center) ? (digit_center - wave_center) : (wave_center - digit_center);
        if (dist > (total_length / 2u)) dist = total_length - dist;

        uint8_t brightness_percent = min_percent;
        if (dist < wave_radius) {
            uint32_t ratio = (dist * 255u) / wave_radius;
            uint32_t add_val = (fx_ease_curve(ratio) * span_percent) >> 8;
            brightness_percent += (uint8_t)add_val;
        }
        uint8_t linear = (uint8_t)((uint32_t)base * brightness_percent / 100u);
        display_ll_set_brightness(i, display_ll_apply_gamma(linear));
    }
}

/* Glitch: Случайная подмена битов в случайном разряде. */
static void fx_apply_glitch(uint32_t elapsed_ms) {
    uint8_t digits = g_display->digit_count;
    if (!g_display->fx_glitch_active) {
        if (elapsed_ms >= g_display->fx_glitch_next_ms) {
            uint8_t digit = (uint8_t)display_rng_range(digits);
            uint8_t bit   = (uint8_t)display_rng_range(7);
            g_display->fx_glitch_digit = digit;
            g_display->fx_glitch_bit = bit;
            g_display->fx_glitch_saved_digit = g_display->content_buffer[digit];
            g_display->fx_glitch_step = 0;
            g_display->fx_glitch_last_ms = elapsed_ms;
            g_display->fx_glitch_active = true;
        }
        return;
    }
    uint32_t frame_ms = g_display->fx_frame_ms ? g_display->fx_frame_ms : 50u;
    if (elapsed_ms - g_display->fx_glitch_last_ms < frame_ms) return;
    g_display->fx_glitch_last_ms = elapsed_ms;

    static const uint8_t pattern[] = {1, 0, 1, 0, 1, 1, 0};
    uint32_t pattern_len = 7;
    uint8_t d = g_display->fx_glitch_digit;
    uint8_t b = g_display->fx_glitch_bit;

    if (d >= digits) { g_display->fx_glitch_active = false; return; }

    vfd_segment_map_t seg = g_display->fx_glitch_saved_digit;
    if (pattern[g_display->fx_glitch_step % pattern_len]) seg |= (1u << b);
    else seg &= ~(1u << b);
    
    display_ll_set_digit_raw(d, seg);
    g_display->fx_glitch_step++;

    if (g_display->fx_glitch_step >= pattern_len) {
        display_ll_set_digit_raw(d, g_display->fx_glitch_saved_digit);
        g_display->fx_glitch_active = false;
        uint32_t interval = 200u + (uint32_t)display_rng_range(601u);
        g_display->fx_glitch_next_ms = elapsed_ms + interval;
    }
}

/* Scanner (Matrix): Эффект бегущего огня (KITT) с затуханием. */
/* 
 * FIX #17: Использование fx_frame_ms как периода эффекта. 
 */
static void fx_apply_matrix(uint32_t elapsed_ms) 
{
    uint32_t duration_ms = g_display->fx_duration_ms;
    if (duration_ms == 0) return;

    uint8_t digits = g_display->digit_count;
    uint8_t base = g_display->fx_base_brightness;

    // Period теперь берется из настроек, а не хардкодится.
    uint32_t period = g_display->fx_frame_ms;
    if (period == 0) period = 1200; 

    uint32_t phase = elapsed_ms % period;
    
    int32_t head_pos_x100;
    
    if (phase < (period / 2)) {
        head_pos_x100 = (int32_t)(phase * (digits - 1) * 100) / (period / 2);
    } else {
        uint32_t phase_back = phase - (period / 2);
        head_pos_x100 = (int32_t)((digits - 1) * 100) - 
                        (int32_t)(phase_back * (digits - 1) * 100) / (period / 2);
    }

    const int32_t width_x100 = 120; 

    for (uint8_t i = 0; i < digits; i++) {
        int32_t my_pos_x100 = i * 100;
        int32_t dist = my_pos_x100 - head_pos_x100;
        if (dist < 0) dist = -dist;

        uint8_t target_brightness = 5; 

        if (dist < width_x100) {
            int32_t intensity = ((width_x100 - dist) * 100) / width_x100;
            int32_t level = 5 + intensity; 
            if (level > 100) level = 100;
            target_brightness = (uint8_t)level;
        }

        uint32_t final_val = (uint32_t)base * target_brightness / 100u;
        display_ll_set_brightness(i, display_ll_apply_gamma((uint8_t)final_val));
    }
}

/* Morph: Побитовое превращение одного буфера в другой. */
static void fx_apply_morph(uint32_t elapsed_ms, uint32_t duration_ms) {
    uint32_t steps = g_display->fx_morph_steps;
    if (steps == 0) return;
    uint32_t step = (uint32_t)((uint64_t)elapsed_ms * steps / duration_ms);
    if (step > steps) step = steps;
    if (step == g_display->fx_morph_step) return;
    g_display->fx_morph_step = step;

    uint8_t digits = g_display->digit_count;
    uint32_t total_pos = (uint32_t)digits * 8u;
    uint32_t threshold = (uint32_t)((uint64_t)step * 255u / steps);

    for (uint8_t d = 0; d < digits; d++) {
        vfd_segment_map_t from = g_display->fx_morph_start[d];
        vfd_segment_map_t to = g_display->fx_morph_target[d];
        vfd_segment_map_t result = from & to; 

        for (uint8_t b = 0; b < 8u; b++) {
            vfd_segment_map_t mask = (1u << b);
            if ((from & mask) == (to & mask)) continue;
            uint32_t weight = ((uint32_t)d * 8u + b) * 255u / total_pos;
            if (threshold >= weight) { if (to & mask) result |= mask; else result &= ~mask; }
            else { if (from & mask) result |= mask; else result &= ~mask; }
        }
        display_ll_set_digit_raw(d, result);
    }
}

/* Dissolve: Случайное выключение сегментов до полного гашения. */
static void fx_apply_dissolve(uint32_t elapsed_ms, uint32_t duration_ms) {
    uint32_t total = g_display->fx_dissolve_total_bits;
    if (total == 0) return;
    uint32_t step = (uint32_t)((uint64_t)elapsed_ms * total / duration_ms);
    if (step > total) step = total;
    if (step == g_display->fx_dissolve_step) return;
    g_display->fx_dissolve_step = step;

    vfd_segment_map_t segs[VFD_MAX_DIGITS];
    uint8_t digits = g_display->digit_count;
    for(int i=0; i<digits; i++) segs[i] = g_display->content_buffer[i];

    for(uint32_t i=0; i<step; i++) {
        uint32_t idx = g_display->fx_dissolve_order[i];
        segs[idx/8] &= ~(1u << (idx%8));
    }
    for(int i=0; i<digits; i++) display_ll_set_digit_raw(i, segs[i]);
}

/* Marquee: Бегущая строка (справа налево). */
static void fx_apply_marquee(uint32_t elapsed_ms) {
    uint8_t digits = g_display->digit_count;
    uint32_t speed = g_display->fx_frame_ms;
    if (speed == 0) speed = 200;

    uint32_t step = elapsed_ms / speed;
    int total_len = g_display->fx_text_len + digits;
    
    if (step >= total_len) {
        for(int i=0; i<digits; i++) display_ll_set_digit_raw(i, 0);
        return;
    }

    for (uint8_t i = 0; i < digits; i++) {
        int char_idx = (int)step - (int)digits + 1 + i;
        vfd_segment_map_t seg = 0;
        if (char_idx >= 0 && char_idx < g_display->fx_text_len) {
            seg = display_font_get_char(g_display->fx_text_buffer[char_idx]);
        }
        display_ll_set_digit_raw(i, seg);
    }
}

/* Slide In: Выезд текста справа с фиксацией. */
static void fx_apply_slide_in(uint32_t elapsed_ms) {
    uint8_t digits = g_display->digit_count;
    uint32_t speed = g_display->fx_frame_ms;
    if (speed == 0) speed = 150;

    uint32_t step = elapsed_ms / speed;
    if (step > digits) step = digits;

    for (uint8_t i = 0; i < digits; i++) {
        int char_idx = (int)i - (int)(digits - step);
        vfd_segment_map_t seg = 0;
        if (char_idx >= 0 && char_idx < g_display->fx_text_len) {
            seg = display_font_get_char(g_display->fx_text_buffer[char_idx]);
        }
        display_ll_set_digit_raw(i, seg);
    }
}

// ============================================================================
//   PUBLIC API
// ============================================================================

bool display_fx_fade_in(uint32_t duration_ms) { return fx_start_basic(FX_FADE_IN, duration_ms, 0); }
bool display_fx_fade_out(uint32_t duration_ms) { return fx_start_basic(FX_FADE_OUT, duration_ms, 0); }
bool display_fx_pulse(uint32_t duration_ms) { return fx_start_basic(FX_PULSE, duration_ms, 0); }
bool display_fx_wave(uint32_t duration_ms) { return fx_start_basic(FX_WAVE, duration_ms, 0); }
bool display_fx_glitch(uint32_t duration_ms) { return fx_start_basic(FX_GLITCH, duration_ms, 30); }

/* Эффект Matrix заменен на Scanner (KITT), но имя API сохранено для совместимости. 
 *
 * FIX #17: Поменяли дефолтное значение frame_ms с 80 на 1200.
 * Теперь frame_ms трактуется как ПЕРИОД эффекта.
 */
bool display_fx_matrix(uint32_t duration_ms, uint32_t frame_ms) {
    if (!fx_start_basic(FX_MATRIX, duration_ms, frame_ms ? frame_ms : 1200)) return false;
    return true;
}

bool display_fx_morph(uint32_t duration_ms, const vfd_segment_map_t *target, uint32_t steps) {
    if (!target || steps==0) return false;
    if (!fx_start_basic(FX_MORPH, duration_ms, duration_ms/steps)) return false;
    g_display->fx_morph_steps = steps;
    for(int i=0; i<g_display->digit_count; i++) {
        g_display->fx_morph_start[i] = g_display->content_buffer[i];
        g_display->fx_morph_target[i] = target[i];
    }
    return true;
}

bool display_fx_dissolve(uint32_t duration_ms) {
    uint8_t digits = g_display->digit_count;
    uint32_t total = digits * 8;
    if (!fx_start_basic(FX_DISSOLVE, duration_ms, duration_ms/total)) return false;
    g_display->fx_dissolve_total_bits = total;
    for(uint32_t i=0; i<total; i++) g_display->fx_dissolve_order[i] = i;
    
    // Перемешивание порядка сегментов
    for(uint32_t i=total-1; i>0; i--) {
        uint32_t j = display_rng_range(i+1);
        uint8_t t = g_display->fx_dissolve_order[i];
        g_display->fx_dissolve_order[i] = g_display->fx_dissolve_order[j];
        g_display->fx_dissolve_order[j] = t;
    }
    return true;
}

bool display_fx_marquee(const char *text, uint32_t speed_ms) {
    if (!text) return false;
    uint16_t len = strlen(text);
    if (len == 0) return false;
    
    uint32_t total_steps = len + g_display->digit_count; 
    uint32_t duration = total_steps * speed_ms;

    if (!fx_start_basic(FX_MARQUEE, duration + speed_ms, speed_ms)) return false;
    fx_prepare_text(text);
    return true;
}

bool display_fx_slide_in(const char *text, uint32_t speed_ms) {
    if (!text) return false;
    uint32_t duration = g_display->digit_count * speed_ms;

    if (!fx_start_basic(FX_SLIDE_IN, duration + speed_ms, speed_ms)) return false;
    fx_prepare_text(text);
    return true;
}

void display_fx_stop(void) {
    if (!g_display->fx_active) return;
    fx_finish_internal();
}

/* Главный тик анимации. Вызывается из display_core. */
void display_fx_tick(void) {
    if (!g_display->fx_active) return;
    
    absolute_time_t now = get_absolute_time();
    uint32_t elapsed_ms = fx_elapsed_ms(g_display->fx_start_time, now);
    g_display->fx_elapsed_ms = elapsed_ms;

    if (g_display->fx_duration_ms != 0 && elapsed_ms >= g_display->fx_duration_ms) {
        fx_finish_internal();
        return;
    }

    switch (g_display->fx_type) {
        case FX_FADE_IN:  fx_apply_fade(elapsed_ms, g_display->fx_duration_ms, false); break;
        case FX_FADE_OUT: fx_apply_fade(elapsed_ms, g_display->fx_duration_ms, true); break;
        case FX_PULSE:    fx_apply_pulse(elapsed_ms, g_display->fx_duration_ms); break;
        case FX_WAVE:     fx_apply_wave(elapsed_ms, g_display->fx_duration_ms); break;
        case FX_GLITCH:   fx_apply_glitch(elapsed_ms); break;
        case FX_MATRIX:   fx_apply_matrix(elapsed_ms); break;
        case FX_MORPH:    fx_apply_morph(elapsed_ms, g_display->fx_duration_ms); break;
        case FX_DISSOLVE: fx_apply_dissolve(elapsed_ms, g_display->fx_duration_ms); break;
        case FX_MARQUEE:  fx_apply_marquee(elapsed_ms); break;
        case FX_SLIDE_IN: fx_apply_slide_in(elapsed_ms); break;
        default: fx_finish_internal(); break;
    }
}

bool display_fx_is_running(void) { return g_display->fx_active; }