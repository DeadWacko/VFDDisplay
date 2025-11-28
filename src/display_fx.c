#include "display_api.h"
#include "display_ll.h"
#include "display_state.h"
#include "display_rng.h"
#include "display_lut.h"
#include "logging.h"

#include "pico/stdlib.h"

#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 *   IMMORTAL FX ENGINE — FADE / PULSE / WAVE / GLITCH / MATRIX / MORPH / DISSOLVE
 * ========================================================================== */

static bool s_rng_seeded = false;

/* ============================================================================
 *   Внутренние утилиты
 * ========================================================================== */

static inline uint32_t fx_elapsed_ms(absolute_time_t from, absolute_time_t to)
{
    int64_t diff_us = absolute_time_diff_us(from, to);
    if (diff_us <= 0) return 0;
    uint64_t diff_ms = (uint64_t)diff_us / 1000u;
    if (diff_ms > 0xFFFFFFFFu) return 0xFFFFFFFFu;
    return (uint32_t)diff_ms;
}

static void fx_seed_rng_if_needed(void)
{
    if (s_rng_seeded) return;

    display_rng_seed_from_adc((uint16_t)g_display->adc_pin);
    s_rng_seeded = true;
}

/* Восстановление яркости (fallback, если snapshot недоступен) */
static void fx_restore_brightness_fallback(void)
{
    uint8_t base = g_display->fx_base_brightness;
    if (base > VFD_MAX_BRIGHTNESS) {
        base = VFD_MAX_BRIGHTNESS;
    }
    display_ll_set_brightness_all(base);
}

/* Общий финал эффекта */
static void fx_finish_internal(void)
{
    if (!g_display->fx_active) return;

    fx_type_t finished_type = g_display->fx_type;

    /* --- Восстановление контента и яркости из snapshot, если есть --- */
    if (g_display->saved_valid) {
        uint8_t digits = g_display->digit_count;
        if (digits > VFD_MAX_DIGITS) {
            digits = VFD_MAX_DIGITS;
        }

        for (uint8_t i = 0; i < digits; i++) {
            vfd_seg_t seg   = g_display->saved_content_buffer[i];
            uint8_t   level = g_display->saved_brightness[i];

            display_ll_set_digit_raw(i, seg);
            display_ll_set_brightness(i, level);

            g_display->final_brightness[i] = level;
        }

        g_display->saved_valid = false;
    } else {
        /* Если snapshot по какой-то причине невалиден — хотя бы восстановим глобальную яркость */
        fx_restore_brightness_fallback();
    }

    g_display->fx_active = false;
    g_display->fx_type   = FX_NONE;

    if (g_display->on_effect_finished) {
        g_display->on_effect_finished(finished_type);
    }
}

/* ============================================================================
 *   Старт FX (общий)
 * ========================================================================== */

static bool fx_can_start(void)
{
    if (!g_display->initialized) {
        LOG_WARN("FX: cannot start, HL not initialized");
        return false;
    }

    if (g_display->ov_active) {
        LOG_WARN("FX: cannot start, overlay is active");
        return false;
    }

    if (g_display->fx_active) {
        LOG_WARN("FX: cannot start, FX already active");
        return false;
    }

    return true;
}

/*
 * Базовый старт эффекта: заполняет общие поля и готовит состояние.
 * Здесь же делается snapshot контента и яркости.
 */
static bool fx_start_basic(fx_type_t type, uint32_t duration_ms, uint32_t frame_ms)
{
    if (!fx_can_start()) {
        return false;
    }

    if (duration_ms == 0) {
        LOG_WARN("FX: duration_ms == 0 is invalid for this effect");
        return false;
    }

    fx_seed_rng_if_needed();

    /* --- Snapshot контента и яркости до эффекта --- */
    uint8_t digits = g_display->digit_count;
    if (digits > VFD_MAX_DIGITS) {
        digits = VFD_MAX_DIGITS;
    }

    for (uint8_t i = 0; i < digits; i++) {
        g_display->saved_content_buffer[i] = g_display->content_buffer[i];
        g_display->saved_brightness[i]     = g_display->final_brightness[i];
    }
    g_display->saved_valid = true;

    g_display->fx_active       = true;
    g_display->fx_type         = type;
    g_display->fx_start_time   = get_absolute_time();
    g_display->fx_duration_ms  = duration_ms;
    g_display->fx_frame_ms     = frame_ms;
    g_display->fx_elapsed_ms   = 0;
    g_display->fx_total_steps  = 0;
    g_display->fx_current_step = 0;

    /* Базовая яркость для глобальных FX (fade/pulse/wave/matrix) */
    uint8_t base = g_display->final_brightness[0];
    if (base == 0) {
        base = VFD_MAX_BRIGHTNESS;
    }
    g_display->fx_base_brightness = base;

    /* Сброс внутреннего состояния GLITCH */
    g_display->fx_glitch_active      = false;
    g_display->fx_glitch_last_ms     = 0;
    g_display->fx_glitch_next_ms     = 0;
    g_display->fx_glitch_step        = 0;
    g_display->fx_glitch_digit       = 0xFF;
    g_display->fx_glitch_bit         = 0xFF;
    g_display->fx_glitch_saved_digit = 0;

    /* MATRIX / MORPH / DISSOLVE инициализируем в ноль.
       Конкретные поля для этих эффектов будут настроены в их старт-функциях. */
    g_display->fx_matrix_last_ms     = 0;
    g_display->fx_matrix_step        = 0;
    g_display->fx_matrix_total_steps = 0;

    g_display->fx_morph_step  = 0;
    g_display->fx_morph_steps = 0;

    g_display->fx_dissolve_total_bits = 0;
    g_display->fx_dissolve_step       = 0;

    return true;
}

/* ============================================================================
 *   Внутренняя логика FADE-IN / FADE-OUT
 * ========================================================================== */

static void fx_apply_fade(uint32_t t_ms, uint32_t duration_ms, bool reverse)
{
    if (duration_ms == 0) {
        return;
    }

    if (t_ms > duration_ms) {
        t_ms = duration_ms;
    }

    uint8_t base = g_display->fx_base_brightness;
    if (base > VFD_MAX_BRIGHTNESS) {
        base = VFD_MAX_BRIGHTNESS;
    }

    uint32_t num;
    if (!reverse) {
        num = (uint32_t)t_ms * (uint32_t)base;
    } else {
        num = (uint32_t)(duration_ms - t_ms) * (uint32_t)base;
    }

    uint8_t linear = (uint8_t)(num / (duration_ms ? duration_ms : 1u)); // 0..base

    uint8_t gamma_level = display_ll_apply_gamma(linear);

    display_ll_set_brightness_all(gamma_level);
}

/* ============================================================================
 *   Внутренняя логика PULSE (breathing)
 * ========================================================================== */

/*
 * PULSE:
 *  - Яркость "дышит" вокруг базовой.
 *  - Используем cos LUT: display_cos_lut[0..255] = 0..255 ≈ (cos+1)/2.
 *  - Делаем 2 полных цикла за duration_ms.
 */

static void fx_apply_pulse(uint32_t t_ms, uint32_t duration_ms)
{
    if (duration_ms == 0) {
        return;
    }

    uint8_t base = g_display->fx_base_brightness;
    if (base > VFD_MAX_BRIGHTNESS) {
        base = VFD_MAX_BRIGHTNESS;
    }

    const uint8_t  min_percent  = 8;       // минимальная яркость в % от base
    const uint8_t  span_percent = 100u - min_percent;
    const uint32_t cycles       = 2;       // 2 вдох-выдоха за эффект

    if (t_ms > duration_ms) {
        t_ms = duration_ms;
    }

    uint32_t phase_full = (uint32_t)t_ms * 256u * cycles / duration_ms;
    uint8_t  phase_idx  = (uint8_t)(phase_full & 0xFFu);

    uint8_t cos_q = display_cos_lut[phase_idx];  // 0..255

    uint32_t tmp         = (uint32_t)cos_q * (uint32_t)span_percent;
    uint8_t  dyn_percent = (uint8_t)(tmp / 255u);   // 0..span_percent

    uint8_t brightness_percent = (uint8_t)(min_percent + dyn_percent); // 8..100

    uint32_t level_num = (uint32_t)base * (uint32_t)brightness_percent;
    uint8_t  linear    = (uint8_t)(level_num / 100u);   // 0..base

    uint8_t gamma_level = display_ll_apply_gamma(linear);

    display_ll_set_brightness_all(gamma_level);
}

/* ============================================================================
 *   Внутренняя логика WAVE
 * ========================================================================== */

// Вспомогательная функция для мягкого спада (имитация гауссоиды/синуса)
// Вход: dist_ratio (0..255), где 0 - центр волны, 255 - край
// Выход: коэффициент яркости (0..255)
static uint8_t fx_ease_curve(uint32_t dist_ratio)
{
    if (dist_ratio >= 255) return 0;

    // Инвертируем: 255 (центр) -> 0 (край)
    uint32_t x = 255 - dist_ratio;

    // Формула "Ease In/Out": y = x^2 / 255.
    // Это делает пик более "круглым", а хвосты более мягкими.
    return (uint8_t)((x * x) >> 8);
}

static void fx_apply_wave(uint32_t t_ms, uint32_t duration_ms)
{
    if (duration_ms == 0) return;

    uint8_t digits = g_display->digit_count;
    if (digits == 0 || digits > VFD_MAX_DIGITS) return;

    uint8_t base = g_display->fx_base_brightness;
    if (base > VFD_MAX_BRIGHTNESS) {
        base = VFD_MAX_BRIGHTNESS;
    }

    // Настройки
    const uint8_t  min_percent  = 20;   // минимальная яркость
    const uint8_t  span_percent = 80;   // сколько "добрасывает" волна сверху (20..100%)
    const uint32_t cycles       = 2;    // сколько раз волна пробежит за duration_ms

    // Ограничиваем время
    if (t_ms > duration_ms) t_ms = duration_ms;

    // 1. Вычисляем абсолютную позицию центра волны в масштабе "256 шагов на разряд"
    // Полный круг = digits * 256
    uint32_t total_length = (uint32_t)digits * 256u;

    // Текущая позиция волны (0 .. total_length)
    uint32_t wave_center = (uint32_t)((uint64_t)t_ms * cycles * total_length / duration_ms) % total_length;

    // Ширина волны (радиус влияния).
    // 384 = 1.5 разряда в каждую сторону (всего пятно света на ~3 разряда)
    const uint32_t wave_radius = 384u;

    for (uint8_t i = 0; i < digits; i++) {
        // Позиция центра текущего разряда (+128 ставит точку в середину знакоместа)
        uint32_t digit_center = (uint32_t)i * 256u + 128u;

        // 2. Вычисляем ЦИКЛИЧЕСКОЕ расстояние (shortest distance on circle)
        uint32_t dist = (digit_center > wave_center)
                        ? (digit_center - wave_center)
                        : (wave_center - digit_center);

        // Если расстояние больше половины круга, значит короче идти через границу
        if (dist > (total_length / 2u)) {
            dist = total_length - dist;
        }

        uint8_t brightness_percent = min_percent;

        // 3. Применяем кривую яркости
        if (dist < wave_radius) {
            // Нормализуем расстояние от 0 до 255 для нашей функции кривой
            uint32_t ratio = (dist * 255u) / wave_radius;

            // Получаем "мягкий" коэффициент (0..255)
            uint32_t curve_val = fx_ease_curve(ratio);

            // Масштабируем его на span_percent
            uint32_t add_val = (curve_val * span_percent) >> 8;

            brightness_percent = (uint8_t)(brightness_percent + add_val);
        }

        // Финальный расчет яркости
        uint32_t level_num = (uint32_t)base * (uint32_t)brightness_percent;
        uint8_t  linear    = (uint8_t)(level_num / 100u);

        uint8_t gamma_level = display_ll_apply_gamma(linear);
        display_ll_set_brightness(i, gamma_level);
    }
}

/* ============================================================================
 *   Внутренняя логика GLITCH
 * ========================================================================== */

static void fx_apply_glitch(uint32_t elapsed_ms)
{
    uint8_t digits = g_display->digit_count;
    if (digits == 0 || digits > VFD_MAX_DIGITS) return;

    /* 1. Если сейчас НЕТ активного бёрста — проверяем, не пора ли его запустить */
    if (!g_display->fx_glitch_active) {
        if (elapsed_ms >= g_display->fx_glitch_next_ms) {
            // Стартуем новый короткий бёрст
            uint8_t digit = (uint8_t)display_rng_range(digits);  // какой разряд
            uint8_t bit   = (uint8_t)display_rng_range(7);       // какой бит (0..6)

            g_display->fx_glitch_digit       = digit;
            g_display->fx_glitch_bit         = bit;
            g_display->fx_glitch_saved_digit = g_display->content_buffer[digit]; // базовый сегмент

            g_display->fx_glitch_step    = 0;
            g_display->fx_glitch_last_ms = elapsed_ms;
            g_display->fx_glitch_active  = true;
        }
        return; // пока ничего не делаем
    }

    /* 2. Идёт активный бёрст — работаем по шагам */
    uint32_t frame_ms = g_display->fx_frame_ms ? g_display->fx_frame_ms : 50u;

    if (elapsed_ms - g_display->fx_glitch_last_ms < frame_ms) {
        return; // ещё не пришло время следующего шага
    }

    g_display->fx_glitch_last_ms = elapsed_ms;

    // Паттерн из старого кода: 1,0,1,0,1,1,0
    static const uint8_t pattern[] = {1, 0, 1, 0, 1, 1, 0};
    const uint32_t pattern_len = sizeof(pattern) / sizeof(pattern[0]);

    uint8_t d = g_display->fx_glitch_digit;
    uint8_t b = g_display->fx_glitch_bit;

    if (d >= digits) {
        g_display->fx_glitch_active = false;
        return;
    }

    vfd_seg_t base = g_display->fx_glitch_saved_digit;
    vfd_seg_t mask = (vfd_seg_t)1u << b;
    vfd_seg_t seg  = base;

    if (pattern[g_display->fx_glitch_step % pattern_len]) {
        seg |= mask;           // включаем бит
    } else {
        seg &= (vfd_seg_t)~mask;  // выключаем бит
    }

    // шлём изменённый разряд сразу в железо
    display_ll_set_digit_raw(d, seg);

    g_display->fx_glitch_step++;

    // паттерн закончился → возвращаем оригинал и планируем следующий бёрст
    if (g_display->fx_glitch_step >= pattern_len) {
        seg = g_display->fx_glitch_saved_digit;
        display_ll_set_digit_raw(d, seg);

        g_display->fx_glitch_active = false;

        // случайная пауза 200..800 мс до следующего бёрста (как было в старом коде)
        uint32_t interval = 200u + (uint32_t)display_rng_range(601u); // 0..600 → 200..800
        g_display->fx_glitch_next_ms = elapsed_ms + interval;
    }
}

/* ============================================================================
 *   Внутренняя логика MATRIX
 * ========================================================================== */

static void fx_apply_matrix(uint32_t elapsed_ms)
{
    uint8_t digits = g_display->digit_count;
    if (digits == 0 || digits > VFD_MAX_DIGITS) return;

    uint32_t frame_ms = g_display->fx_frame_ms ? g_display->fx_frame_ms : 80u;
    if (elapsed_ms - g_display->fx_matrix_last_ms < frame_ms) {
        return;
    }
    g_display->fx_matrix_last_ms = elapsed_ms;

    uint8_t base = g_display->fx_base_brightness;
    if (base > VFD_MAX_BRIGHTNESS) {
        base = VFD_MAX_BRIGHTNESS;
    }

    uint8_t min_percent = g_display->fx_matrix_min_percent;
    if (min_percent > 100u) {
        min_percent = 100u;
    }

    for (uint8_t i = 0; i < digits; i++) {
        uint8_t cur = g_display->fx_matrix_brightness_percent[i];

        if (cur < min_percent || cur > 100u) {
            cur = (uint8_t)(min_percent + display_rng_range(101u - min_percent));
        }

        uint8_t r = (uint8_t)display_rng_range(100u);

        if (r < 20u) {
            // 20% шанс вспышки до максимума
            cur = 100u;
        } else if (r < 50u) {
            // 30% шанс "провала" к нижней границе
            uint8_t span = (uint8_t)(101u - min_percent);
            cur = (uint8_t)(min_percent + display_rng_range(span));
        } else {
            // Иначе — небольшой дрейф
            int8_t step = (int8_t)display_rng_range(7u) - 3; // -3..+3
            int16_t tmp = (int16_t)cur + step;
            if (tmp < (int16_t)min_percent) tmp = (int16_t)min_percent;
            if (tmp > 100) tmp = 100;
            cur = (uint8_t)tmp;
        }

        g_display->fx_matrix_brightness_percent[i] = cur;

        uint32_t level_num = (uint32_t)base * (uint32_t)cur;
        uint8_t  linear    = (uint8_t)(level_num / 100u);

        uint8_t gamma_level = display_ll_apply_gamma(linear);
        display_ll_set_brightness(i, gamma_level);
    }
}

/* ============================================================================
 *   Внутренняя логика MORPH
 * ========================================================================== */

static void fx_apply_morph(uint32_t elapsed_ms, uint32_t duration_ms)
{
    uint8_t digits = g_display->digit_count;
    if (digits == 0 || digits > VFD_MAX_DIGITS) return;
    if (duration_ms == 0) return;
    if (g_display->fx_morph_steps == 0) return;

    uint32_t steps = g_display->fx_morph_steps;
    uint32_t step = (uint32_t)((uint64_t)elapsed_ms * steps / duration_ms);
    if (step > steps) {
        step = steps;
    }

    if (step == g_display->fx_morph_step) {
        return; // ничего нового
    }
    g_display->fx_morph_step = step;

    if (steps == 0) return;

    uint32_t total_positions = (uint32_t)digits * 8u;
    if (total_positions == 0) total_positions = 1;

    uint32_t threshold = (uint32_t)((uint64_t)step * 255u / steps);

    for (uint8_t d = 0; d < digits; d++) {
        vfd_seg_t from = g_display->fx_morph_start[d];
        vfd_seg_t to   = g_display->fx_morph_target[d];

        vfd_seg_t same = from & to;

        vfd_seg_t result = same;

        for (uint8_t b = 0; b < 8u; b++) {
            vfd_seg_t mask = (vfd_seg_t)1u << b;
            bool from_bit = (from & mask) != 0;
            bool to_bit   = (to   & mask) != 0;

            if (from_bit == to_bit) {
                // Уже учтено в same, ничего не делаем
                continue;
            }

            uint32_t pos_index = (uint32_t)d * 8u + (uint32_t)b;
            uint32_t weight    = (pos_index * 255u) / total_positions;

            bool use_target = (threshold >= weight);
            bool bit_val    = use_target ? to_bit : from_bit;

            if (bit_val) {
                result |= mask;
            } else {
                result &= (vfd_seg_t)~mask;
            }
        }

        display_ll_set_digit_raw(d, result);
    }
}

/* ============================================================================
 *   Внутренняя логика DISSOLVE
 * ========================================================================== */

static void fx_apply_dissolve(uint32_t elapsed_ms, uint32_t duration_ms)
{
    uint8_t digits = g_display->digit_count;
    if (digits == 0 || digits > VFD_MAX_DIGITS) return;
    if (duration_ms == 0) return;

    uint32_t total_bits = g_display->fx_dissolve_total_bits;
    if (total_bits == 0) return;

    uint32_t step = (uint32_t)((uint64_t)elapsed_ms * total_bits / duration_ms);
    if (step > total_bits) {
        step = total_bits;
    }

    if (step == g_display->fx_dissolve_step) {
        return; // нечего обновлять
    }
    g_display->fx_dissolve_step = step;

    /* Строим временный массив сегментов на основе исходного контента и
       "выключаем" первые step битов в порядке fx_dissolve_order[]. */

    vfd_seg_t segs[VFD_MAX_DIGITS];

    for (uint8_t d = 0; d < digits; d++) {
        segs[d] = g_display->content_buffer[d];
    }

    for (uint32_t i = 0; i < step; i++) {
        uint32_t idx = g_display->fx_dissolve_order[i];
        uint8_t  d   = (uint8_t)(idx / 8u);
        uint8_t  b   = (uint8_t)(idx % 8u);

        if (d >= digits) continue;

        vfd_seg_t mask = (vfd_seg_t)1u << b;
        segs[d] &= (vfd_seg_t)~mask;
    }

    for (uint8_t d = 0; d < digits; d++) {
        display_ll_set_digit_raw(d, segs[d]);
    }
}

/* ============================================================================
 *   Публичный API FX
 * ========================================================================== */

bool display_fx_fade_in(uint32_t duration_ms)
{
    if (!fx_start_basic(FX_FADE_IN, duration_ms, 0)) {
        return false;
    }
    LOG_INFO("FX: fade-in start, duration=%u ms", duration_ms);
    return true;
}

bool display_fx_fade_out(uint32_t duration_ms)
{
    if (!fx_start_basic(FX_FADE_OUT, duration_ms, 0)) {
        return false;
    }
    LOG_INFO("FX: fade-out start, duration=%u ms", duration_ms);
    return true;
}

bool display_fx_pulse(uint32_t duration_ms)
{
    if (!fx_start_basic(FX_PULSE, duration_ms, 0)) {
        return false;
    }
    LOG_INFO("FX: pulse start, duration=%u ms", duration_ms);
    return true;
}

bool display_fx_wave(uint32_t duration_ms)
{
    if (!fx_start_basic(FX_WAVE, duration_ms, 0)) {
        return false;
    }
    LOG_INFO("FX: wave start, duration=%u ms", duration_ms);
    return true;
}

bool display_fx_glitch(uint32_t duration_ms)
{
    // frame_ms ~ 30мс для "шуршания"
    if (!fx_start_basic(FX_GLITCH, duration_ms, 30)) {
        return false;
    }
    LOG_INFO("FX: glitch start, duration=%u ms", duration_ms);
    return true;
}

bool display_fx_matrix(uint32_t duration_ms, uint32_t frame_ms)
{
    if (frame_ms == 0) {
        frame_ms = 80u;
    }

    if (!fx_start_basic(FX_MATRIX, duration_ms, frame_ms)) {
        return false;
    }

    uint8_t digits = g_display->digit_count;
    if (digits > VFD_MAX_DIGITS) {
        digits = VFD_MAX_DIGITS;
    }

    g_display->fx_matrix_total_steps = duration_ms / frame_ms;
    if (g_display->fx_matrix_total_steps == 0) {
        g_display->fx_matrix_total_steps = 1;
    }

    for (uint8_t i = 0; i < digits; i++) {
        g_display->fx_matrix_brightness_percent[i] = 100u;
    }

    LOG_INFO("FX: matrix start, duration=%u ms, frame=%u ms", duration_ms, frame_ms);
    return true;
}

bool display_fx_morph(uint32_t duration_ms, const vfd_seg_t *target, uint32_t steps)
{
    if (target == NULL) {
        LOG_WARN("FX: MORPH target is NULL");
        return false;
    }

    if (steps == 0) {
        LOG_WARN("FX: MORPH steps == 0 is invalid");
        return false;
    }

    uint32_t frame_ms = duration_ms / steps;
    if (frame_ms == 0) {
        frame_ms = 1u;
    }

    if (!fx_start_basic(FX_MORPH, duration_ms, frame_ms)) {
        return false;
    }

    uint8_t digits = g_display->digit_count;
    if (digits > VFD_MAX_DIGITS) {
        digits = VFD_MAX_DIGITS;
    }

    for (uint8_t i = 0; i < digits; i++) {
        g_display->fx_morph_start[i]  = g_display->content_buffer[i];
        g_display->fx_morph_target[i] = target[i];
    }

    g_display->fx_morph_steps = steps;
    g_display->fx_morph_step  = 0;

    LOG_INFO("FX: morph start, duration=%u ms, steps=%u", duration_ms, steps);
    return true;
}

bool display_fx_dissolve(uint32_t duration_ms)
{
    uint8_t digits = g_display->digit_count;
    if (digits == 0 || digits > VFD_MAX_DIGITS) {
        LOG_WARN("FX: DISSOLVE invalid digit count=%u", digits);
        return false;
    }

    uint32_t total_bits = (uint32_t)digits * 8u;
    if (total_bits == 0) {
        LOG_WARN("FX: DISSOLVE total_bits == 0");
        return false;
    }

    /* frame_ms подбираем так, чтобы примерно один бит выключался за шаг */
    uint32_t frame_ms = duration_ms / total_bits;
    if (frame_ms == 0) {
        frame_ms = 10u; // минимальная длительность шага
    }

    if (!fx_start_basic(FX_DISSOLVE, duration_ms, frame_ms)) {
        return false;
    }

    g_display->fx_dissolve_total_bits = total_bits;
    g_display->fx_dissolve_step       = 0;

    /* Заполняем массив индексов и перемешиваем его (Fisher–Yates) */
    for (uint32_t i = 0; i < total_bits; i++) {
        g_display->fx_dissolve_order[i] = (uint8_t)i;
    }

    for (uint32_t i = total_bits - 1; i > 0; i--) {
        uint32_t j = display_rng_range(i + 1u); // 0..i
        uint8_t tmp = g_display->fx_dissolve_order[i];
        g_display->fx_dissolve_order[i] = g_display->fx_dissolve_order[j];
        g_display->fx_dissolve_order[j] = tmp;
    }

    LOG_INFO("FX: dissolve start, duration=%u ms, bits=%u", duration_ms, total_bits);
    return true;
}

/* Остановить текущий эффект и восстановить базовую яркость/контент */
void display_fx_stop(void)
{
    if (!g_display->fx_active) return;

    LOG_INFO("FX: stop requested");
    fx_finish_internal();
}

/* ========================================================================
 *   Основной тик FX
 * ====================================================================== */

void display_fx_tick(void)
{
    if (!g_display->fx_active) {
        return;
    }

    absolute_time_t now = get_absolute_time();
    uint32_t elapsed_ms = fx_elapsed_ms(g_display->fx_start_time, now);
    g_display->fx_elapsed_ms = elapsed_ms;

    uint32_t duration_ms = g_display->fx_duration_ms;

    if (duration_ms != 0 && elapsed_ms >= duration_ms) {
        fx_finish_internal();
        return;
    }

    switch (g_display->fx_type) {
    case FX_FADE_IN:
        fx_apply_fade(elapsed_ms, duration_ms, false);
        break;

    case FX_FADE_OUT:
        fx_apply_fade(elapsed_ms, duration_ms, true);
        break;

    case FX_PULSE:
        fx_apply_pulse(elapsed_ms, duration_ms);
        break;

    case FX_WAVE:
        fx_apply_wave(elapsed_ms, duration_ms);
        break;

    case FX_GLITCH:
        fx_apply_glitch(elapsed_ms);
        break;

    case FX_MATRIX:
        fx_apply_matrix(elapsed_ms);
        break;

    case FX_MORPH:
        fx_apply_morph(elapsed_ms, duration_ms);
        break;

    case FX_DISSOLVE:
        fx_apply_dissolve(elapsed_ms, duration_ms);
        break;

    case FX_NONE:
    default:
        LOG_WARN("FX: tick called for unsupported effect type=%d, finishing", (int)g_display->fx_type);
        fx_finish_internal();
        break;
    }
}

/* ========================================================================
 *   Статус FX
 * ====================================================================== */

bool display_fx_is_running(void)
{
    return g_display->fx_active;
}
