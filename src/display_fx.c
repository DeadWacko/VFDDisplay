#include "display_api.h"
#include "display_ll.h"

#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

/*
 * FX LAYER
 * --------
 * Перенос эффектов из легаси display.c в новую архитектуру:
 * - все эффекты неблокирующие
 * - один активный эффект за раз
 * - обновление только из display_process() через display_fx_tick()
 *
 * Эффекты:
 *   FADE_IN   / FADE_OUT  — плавное появление/исчезновение (с гаммой)
 *   PULSE                  — "дыхание" яркости (cos + gamma + min level)
 *   WAVE                   — волна яркости по разрядам
 *   GLITCH                 — динамический фликер (рандомный бит/разряд)
 *   MATRIX                 — "matrix rain" по яркости разрядов
 *
 * ВАЖНО:
 *   - используем LL-яркость (display_ll_set_brightness/[_all])
 *   - содержимое (сегментные маски) не трогаем, кроме GLITCH
 *   - никаких repeating_timer — только абсолютное время и тик
 */

typedef enum {
    FX_TYPE_NONE = 0,
    FX_TYPE_FADE_IN,
    FX_TYPE_FADE_OUT,
    FX_TYPE_PULSE,
    FX_TYPE_WAVE,
    FX_TYPE_GLITCH,
    FX_TYPE_MATRIX,
} fx_type_t;

typedef struct {
    bool           active;
    fx_type_t      type;

    absolute_time_t start_time;
    uint32_t       duration_ms;     // общая длительность эффекта
    uint32_t       frame_ms;        // используется для MATRIX, GLITCH

    uint8_t        base_brightness; // "базовая" яркость (пока просто 255)

    /* -------- GLITCH (динамический фликер) -------- */
    bool           glitch_active;   // идёт ли сейчас короткий фликер-бёрст
    uint32_t       glitch_last_ms;  // последний шаг фликера
    uint32_t       glitch_next_ms;  // когда стартовать следующий бёрст (от начала эффекта)
    uint32_t       glitch_step;     // шаг внутри паттерна
    uint8_t        glitch_digit;    // какой разряд ковыряем
    uint8_t        glitch_bit;      // какой бит (0..6, DP не трогаем)
    vfd_seg_t      glitch_saved_digit; // исходная маска разряда

    /* -------- MATRIX RAIN -------- */
    uint32_t       matrix_last_ms;
    uint32_t       matrix_step;
    uint32_t       matrix_total_steps;
    uint8_t        matrix_min_percent;                       // минимальная "подсветка"
    uint8_t        matrix_brightness_percent[VFD_MAX_DIGITS]; // 0..100
} fx_state_t;

static fx_state_t s_fx = {
    .active           = false,
    .type             = FX_TYPE_NONE,
    .duration_ms      = 0,
    .frame_ms         = 0,
    .base_brightness  = 255,
    .glitch_active    = false,
    .glitch_last_ms   = 0,
    .glitch_next_ms   = 0,
    .glitch_step      = 0,
    .glitch_digit     = 0xFF,
    .glitch_bit       = 0xFF,
    .glitch_saved_digit = 0,
    .matrix_last_ms   = 0,
    .matrix_step      = 0,
    .matrix_total_steps = 0,
    .matrix_min_percent  = 5,
};

/* ------------------ УТИЛИТЫ ------------------ */

static inline float fx_gamma_correct(float x)
{
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    // как в легаси: ~гамма 2.0
    return x * x;
}

static inline uint32_t fx_elapsed_ms(absolute_time_t from, absolute_time_t to)
{
    int64_t diff_us = absolute_time_diff_us(from, to);
    if (diff_us <= 0) return 0;
    uint64_t diff_ms = (uint64_t)diff_us / 1000u;
    if (diff_ms > 0xFFFFFFFFu) return 0xFFFFFFFFu;
    return (uint32_t)diff_ms;
}

static inline uint8_t fx_digits_count(void)
{
    uint8_t n = display_ll_get_digit_count();
    if (n == 0 || n > VFD_MAX_DIGITS) n = VFD_MAX_DIGITS;
    return n;
}

static void fx_finish(void);

/* ------------------ СТАРТ ЭФФЕКТА ------------------ */

static bool fx_start(fx_type_t type, uint32_t duration_ms, uint32_t frame_ms)
{
    if (s_fx.active) {
        // один эффект за раз — как в спецификации
        return false;
    }

    if (duration_ms == 0) {
        return false;
    }

    s_fx.active          = true;
    s_fx.type            = type;
    s_fx.start_time      = get_absolute_time();
    s_fx.duration_ms     = duration_ms;
    s_fx.base_brightness = 255;    // TODO: если понадобится, можно хранить реальную яркость из ядра

    /* Общая инициализация "расширенных" полей */
    s_fx.frame_ms        = frame_ms;
    s_fx.glitch_active   = false;
    s_fx.glitch_last_ms  = 0;
    s_fx.glitch_next_ms  = 0;
    s_fx.glitch_step     = 0;
    s_fx.glitch_digit    = 0xFF;
    s_fx.glitch_bit      = 0xFF;

    s_fx.matrix_last_ms    = 0;
    s_fx.matrix_step       = 0;
    s_fx.matrix_total_steps = 0;
    s_fx.matrix_min_percent = 5;

    uint32_t seed = to_ms_since_boot(s_fx.start_time);
    srand(seed);

    if (type == FX_TYPE_MATRIX) {
        // Matrix Rain: frame_ms обязан быть >0, иначе подставим дефолт
        if (frame_ms == 0) {
            s_fx.frame_ms = 50; // ~20 FPS
        }

        s_fx.matrix_total_steps = duration_ms / s_fx.frame_ms;
        if (s_fx.matrix_total_steps == 0) s_fx.matrix_total_steps = 1;

        uint8_t digits = fx_digits_count();
        for (uint8_t i = 0; i < digits; i++) {
            s_fx.matrix_brightness_percent[i] = 0;
        }
    }

    if (type == FX_TYPE_GLITCH) {
        // GLITCH: задаём первый старт сразу (next_ms = 0), дальше будет рандом
        s_fx.frame_ms      = 50;  // шаг паттерна ~50 мс, как в легаси
        s_fx.glitch_active = false;
        s_fx.glitch_last_ms= 0;
        s_fx.glitch_next_ms= 0;   // сразу можем запустить первый бёрст
        s_fx.glitch_step   = 0;
    }

    return true;
}

static void fx_finish(void)
{
    // Восстановление состояния после эффекта
    switch (s_fx.type) {
    case FX_TYPE_GLITCH:
        // Если фликер в момент завершения ещё активен — вернём исходный разряд
        if (s_fx.glitch_active && s_fx.glitch_digit != 0xFF) {
            vfd_seg_t *buf = display_ll_get_buffer();
            uint8_t digits = fx_digits_count();
            if (s_fx.glitch_digit < digits) {
                buf[s_fx.glitch_digit] = s_fx.glitch_saved_digit;
            }
        }
        break;

    case FX_TYPE_MATRIX:
        // Вернём всем разрядам базовую яркость
        display_ll_set_brightness_all(s_fx.base_brightness);
        break;

    case FX_TYPE_FADE_IN:
    case FX_TYPE_FADE_OUT:
    case FX_TYPE_PULSE:
    case FX_TYPE_WAVE:
    case FX_TYPE_NONE:
    default:
        // Для остальных — просто ставим базовую яркость
        display_ll_set_brightness_all(s_fx.base_brightness);
        break;
    }

    s_fx.active = false;
    s_fx.type   = FX_TYPE_NONE;
}

/* ------------------ ПУБЛИЧНЫЙ API FX ------------------ */

bool display_fx_fade_in(uint32_t duration_ms)
{
    return fx_start(FX_TYPE_FADE_IN, duration_ms, 0);
}

bool display_fx_fade_out(uint32_t duration_ms)
{
    return fx_start(FX_TYPE_FADE_OUT, duration_ms, 0);
}

bool display_fx_pulse(uint32_t duration_ms)
{
    // В легаси был параметр cycles; здесь выберем дефолт 2 "вдоха" за duration_ms.
    return fx_start(FX_TYPE_PULSE, duration_ms, 0);
}

bool display_fx_wave(uint32_t duration_ms)
{
    return fx_start(FX_TYPE_WAVE, duration_ms, 0);
}

bool display_fx_glitch(uint32_t duration_ms)
{
    // Dynamic flicker из легаси, адаптированный под один параметр duration_ms.
    return fx_start(FX_TYPE_GLITCH, duration_ms, 0);
}

bool display_fx_matrix(uint32_t duration_ms, uint32_t frame_ms)
{
    return fx_start(FX_TYPE_MATRIX, duration_ms, frame_ms);
}

void display_fx_stop(void)
{
    if (!s_fx.active) return;
    fx_finish();
}

/* ------------------ ОБНОВЛЕНИЕ ЭФФЕКТА ------------------ */

void display_fx_tick(void)
{
    if (!s_fx.active) {
        return;
    }

    absolute_time_t now = get_absolute_time();
    uint32_t elapsed_ms = fx_elapsed_ms(s_fx.start_time, now);

    if (elapsed_ms >= s_fx.duration_ms) {
        // Эффект завершился по времени
        fx_finish();
        return;
    }

    float t_norm = (s_fx.duration_ms > 0)
                   ? ((float)elapsed_ms / (float)s_fx.duration_ms)
                   : 0.0f;
    if (t_norm < 0.0f) t_norm = 0.0f;
    if (t_norm > 1.0f) t_norm = 1.0f;

    switch (s_fx.type) {

    /* -------- FADE IN / OUT (как в легаси) -------- */
    case FX_TYPE_FADE_IN: {
        // n = t, brightness ~ gamma(n)
        float n = t_norm;
        float g = fx_gamma_correct(n);
        uint8_t brightness = (uint8_t)(g * 255.0f + 0.5f);
        display_ll_set_brightness_all(brightness);
        break;
    }

    case FX_TYPE_FADE_OUT: {
        // n = 1 - t, brightness ~ gamma(n)
        float n = 1.0f - t_norm;
        float g = fx_gamma_correct(n);
        uint8_t brightness = (uint8_t)(g * 255.0f + 0.5f);
        display_ll_set_brightness_all(brightness);
        break;
    }

    /* -------- PULSE — дыхание, перенос формулы из легаси -------- */
    case FX_TYPE_PULSE: {
        // В легаси:
        // progress -> phi = 2π * cycles * progress
        // val = -cos(phi)
        // normalized = (val + 1)/2
        // normalized = gamma_correct(normalized)
        // brightness = 8 + normalized * (MAX - 8)
        const float cycles = 2.0f; // дефолтные 2 "вдоха" за duration_ms

        float phi = t_norm * 2.0f * (float)M_PI * cycles;
        float val = -cosf(phi);
        float normalized = (val + 1.0f) * 0.5f;      // 0..1
        normalized = fx_gamma_correct(normalized);   // гамма, как в легаси

        const float min_percent = 8.0f;              // 8% минимум, чтобы не тухло совсем
        float brightness_percent = min_percent +
                                   normalized * (100.0f - min_percent); // 8..100

        if (brightness_percent < 0.0f) brightness_percent = 0.0f;
        if (brightness_percent > 100.0f) brightness_percent = 100.0f;

        uint8_t brightness = (uint8_t)(brightness_percent * 255.0f / 100.0f + 0.5f);
        display_ll_set_brightness_all(brightness);
        break;
    }

    /* -------- WAVE — волна яркости по разрядам -------- */
    case FX_TYPE_WAVE: {
        // Легаси: wave_step * 0.18f как time_phase, сдвиг по разрядам 0.7f,
        // gamma, 5% минимум.
        //
        // Здесь time_phase строим по времени:
        // при frame_ms ~20 мс и шаге 0.18 рад на тик → ω ≈ 0.18 / 0.02 = 9 рад/с.
        // значит phase ≈ elapsed_ms * 0.009f.
        float time_phase = (float)elapsed_ms * 0.009f;
        const float phase_shift = 0.7f;
        const float min_percent = 5.0f;

        uint8_t digits = fx_digits_count();
        for (uint8_t i = 0; i < digits; i++) {
            float val = sinf(time_phase - (i * phase_shift));
            float normalized = (val + 1.0f) * 0.5f;     // 0..1
            normalized = fx_gamma_correct(normalized);  // гамма

            float brightness_percent = min_percent +
                                       normalized * (100.0f - min_percent); // 5..100

            if (brightness_percent < 0.0f) brightness_percent = 0.0f;
            if (brightness_percent > 100.0f) brightness_percent = 100.0f;

            uint8_t brightness = (uint8_t)(brightness_percent * 255.0f / 100.0f + 0.5f);
            display_ll_set_brightness(i, brightness);
        }
        break;
    }

    /* -------- GLITCH — динамический фликер (адаптация flicker_cb) -------- */
    case FX_TYPE_GLITCH: {
        // Паттерн из легаси: 1,0,1,0,1,1,0
        static const int pattern[] = {1, 0, 1, 0, 1, 1, 0};
        const int pattern_len = (int)(sizeof(pattern) / sizeof(pattern[0]));
        const uint32_t step_ms = (s_fx.frame_ms ? s_fx.frame_ms : 50); // 50 мс

        uint32_t rel_ms = elapsed_ms; // "локальное" время от старта эффекта

        if (!s_fx.glitch_active) {
            // Ожидаем до glitch_next_ms, после чего запускаем новый короткий бёрст
            if (rel_ms >= s_fx.glitch_next_ms) {
                // выбираем разряд/бит и сохраняем его исходную маску
                uint8_t digits = fx_digits_count();
                if (digits == 0) break;

                vfd_seg_t *buf = display_ll_get_buffer();

                s_fx.glitch_digit = (uint8_t)(rand() % digits);
                s_fx.glitch_bit   = (uint8_t)(rand() % 7); // 0..6, DP не трогаем
                s_fx.glitch_saved_digit = buf[s_fx.glitch_digit];

                s_fx.glitch_step     = 0;
                s_fx.glitch_last_ms  = rel_ms;
                s_fx.glitch_active   = true;
            }
        } else {
            // Идёт бёрст — переключаем сегмент по паттерну раз в step_ms
            if (rel_ms - s_fx.glitch_last_ms >= step_ms) {
                s_fx.glitch_last_ms = rel_ms;

                vfd_seg_t *buf = display_ll_get_buffer();
                uint8_t digits = fx_digits_count();
                if (s_fx.glitch_digit >= digits) {
                    // что-то пошло не так — просто завершим бёрст
                    s_fx.glitch_active = false;
                    break;
                }

                int p = pattern[s_fx.glitch_step % pattern_len];
                vfd_seg_t mask = (vfd_seg_t)(1u << s_fx.glitch_bit);

                if (p) {
                    buf[s_fx.glitch_digit] |= mask;
                } else {
                    buf[s_fx.glitch_digit] &= (vfd_seg_t)~mask;
                }

                s_fx.glitch_step++;

                if (s_fx.glitch_step >= (uint32_t)pattern_len) {
                    // Закончили этот бёрст — возвращаем исходную маску
                    buf[s_fx.glitch_digit] = s_fx.glitch_saved_digit;
                    s_fx.glitch_active = false;

                    // Запланируем следующий бёрст через 200..800 мс
                    uint32_t interval = 200u + (uint32_t)(rand() % 600u);
                    s_fx.glitch_next_ms = rel_ms + interval;
                }
            }
        }
        break;
    }

    /* -------- MATRIX RAIN -------- */
    case FX_TYPE_MATRIX: {
        uint32_t frame_ms = s_fx.frame_ms ? s_fx.frame_ms : 50u;
        if (elapsed_ms - s_fx.matrix_last_ms < frame_ms) {
            break; // ещё не время следующего кадра
        }

        s_fx.matrix_last_ms = elapsed_ms;

        uint8_t digits = fx_digits_count();
        if (digits == 0) break;

        // Плавно уменьшаем яркость всех разрядов
        const uint8_t dec = 5; // скорость затухания "шлейфа"
        for (uint8_t i = 0; i < digits; i++) {
            uint8_t b = s_fx.matrix_brightness_percent[i];
            if (b > s_fx.matrix_min_percent) {
                if (b <= (uint8_t)(s_fx.matrix_min_percent + dec))
                    b = s_fx.matrix_min_percent;
                else
                    b = (uint8_t)(b - dec);
                s_fx.matrix_brightness_percent[i] = b;
            }
        }

        // Зажигаем одну случайную "каплю" на максимум
        uint8_t d = (uint8_t)(rand() % digits);
        s_fx.matrix_brightness_percent[d] = 100;

        // Применяем яркость к LL
        for (uint8_t i = 0; i < digits; i++) {
            uint8_t b = s_fx.matrix_brightness_percent[i];
            uint8_t duty = (uint8_t)((b * 255u) / 100u);
            display_ll_set_brightness(i, duty);
        }

        s_fx.matrix_step++;
        break;
    }

    case FX_TYPE_NONE:
    default:
        // на всякий случай
        break;
    }
}
