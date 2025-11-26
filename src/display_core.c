#include "display_api.h"
#include "display_ll.h"

#include "pico/stdlib.h"
#include <string.h>
#include <stdbool.h>

/*
 * NEW HIGH-LEVEL (HL) CORE
 * ------------------------
 * Отвечает за:
 * - глобальное состояние дисплея
 * - текущий режим (CONTENT / EFFECT / OVERLAY)
 * - глобальную яркость
 * - обновление HL-буфера сегментов
 * - вызов LL для вывода сегментов
 *
 * Пока без эффектов и оверлеев.
 */

// ВРЕМЕННЫЕ дефолтные пины — под  текущий стенд.
// TODO: потом вынести в конфиг / CMake.



bool display_fx_is_running(void);   
void display_overlay_tick(void);

#define DISPLAY_DEFAULT_DATA_PIN     15
#define DISPLAY_DEFAULT_CLOCK_PIN    14
#define DISPLAY_DEFAULT_LATCH_PIN    13
#define DISPLAY_DEFAULT_REFRESH_HZ   120

typedef struct
{
    display_mode_t mode;

    uint8_t digit_count;

    // HL-буфер сегментов, который отображается через LL
    vfd_seg_t hl_buffer[VFD_MAX_DIGITS];

    // Глобальная яркость (0..255)
    uint8_t global_brightness;

    // Для будущих таймеров эффектов/оверлеев
    absolute_time_t last_update;

    // --- мигающая точка/двоеточие ---
    bool           dot_blink_enabled;  // включено ли мигание
    bool           dot_state;          // текущее состояние точки (горит / нет)
    uint32_t       dot_period_ms;      // период мигания, мс
    absolute_time_t dot_last_toggle;   // последний момент переключения
} display_core_state_t;

static display_core_state_t s_core;

/* ============================================================
 *    ВНУТРЕННИЕ УТИЛИТЫ
 * ============================================================ */

static void hl_push_buffer_to_ll(void)
{
    for (uint8_t i = 0; i < s_core.digit_count; i++) {
        display_ll_set_brightness(i, s_core.global_brightness);
        display_ll_set_digit_raw(i, s_core.hl_buffer[i]);
    }
}

/* ============================================================
 *    ПУБЛИЧНЫЕ ФУНКЦИИ (API)
 * ============================================================ */

void display_init(uint8_t digit_count)
{
    if (digit_count == 0 || digit_count > VFD_MAX_DIGITS)
        digit_count = 4;

    display_ll_config_t cfg = {
        .data_pin        = DISPLAY_DEFAULT_DATA_PIN,
        .clock_pin       = DISPLAY_DEFAULT_CLOCK_PIN,
        .latch_pin       = DISPLAY_DEFAULT_LATCH_PIN,
        .digit_count     = digit_count,
        .refresh_rate_hz = DISPLAY_DEFAULT_REFRESH_HZ,
    };

    display_ll_init(&cfg);
    display_ll_start_refresh();

    memset(&s_core, 0, sizeof(s_core));
    s_core.digit_count       = digit_count;
    s_core.global_brightness = VFD_MAX_BRIGHTNESS;

    for (uint8_t i = 0; i < digit_count; i++)
        s_core.hl_buffer[i] = 0;

    s_core.mode        = DISPLAY_MODE_CONTENT;
    s_core.last_update = get_absolute_time();

    // --- инициализация мигания точки/двоеточия ---
    s_core.dot_blink_enabled = false;
    s_core.dot_state         = false;
    s_core.dot_period_ms     = 1000;              // 1 Гц
    s_core.dot_last_toggle   = get_absolute_time();
}

display_mode_t display_get_mode(void)
{
    return s_core.mode;
}

bool display_is_effect_running(void)
{
    return display_fx_is_running();
}


/* ---------- ЯРКОСТЬ ---------- */

void display_set_brightness(uint8_t brightness)
{
    if (brightness > 255)
        brightness = 255;

    s_core.global_brightness = brightness;

    for (uint8_t i = 0; i < s_core.digit_count; i++)
        display_ll_set_brightness(i, brightness);
}

void display_set_auto_brightness(bool enable)
{
    // Пока режим не реализован
    (void)enable;
}

void display_set_night_mode(bool enable)
{
    // Пока нет night-логики
    (void)enable;
}

/* ---------- ТОЧКА / ДВОЕТОЧИЕ ---------- */

void display_set_dot_blinking(bool enable)
{
    s_core.dot_blink_enabled = enable;
    s_core.dot_state         = false;
    s_core.dot_last_toggle   = get_absolute_time();

    if (!enable && s_core.digit_count >= 4) {
        // при выключении мигания просто гасим точки
        s_core.hl_buffer[1] &= (vfd_seg_t)~0x80;
        s_core.hl_buffer[2] &= (vfd_seg_t)~0x80;
        hl_push_buffer_to_ll();
    }
}

/* ============================================================
 *      ПРИНИМАЕМ HL-БУФЕР ОТ CONTENT-ЛОГИКИ
 * ============================================================ */

void display_core_set_buffer(const vfd_seg_t *buf, uint8_t size)
{
    uint8_t n = (size < s_core.digit_count) ? size : s_core.digit_count;
    for (uint8_t i = 0; i < n; i++)
        s_core.hl_buffer[i] = buf[i];

    hl_push_buffer_to_ll();
}

/* ============================================================
 *      MAIN PROCESS LOOP
 * ============================================================ */

void display_process(void)
{
    absolute_time_t now = get_absolute_time();

    // --- мигание точки  (формат HH.MM на 4+ разрядах) ---
    if (s_core.dot_blink_enabled && s_core.digit_count >= 4) {
        uint32_t now_ms  = to_ms_since_boot(now);
        uint32_t last_ms = to_ms_since_boot(s_core.dot_last_toggle);

        if (now_ms - last_ms >= s_core.dot_period_ms) {
            s_core.dot_last_toggle = now;
            s_core.dot_state       = !s_core.dot_state;

            if (s_core.dot_state) {
                // включаем DP (бит 7) у 2 разряда (можно добавить 3-й, если захочешь)
                s_core.hl_buffer[1] |=  0x80;
            } else {
                // выключаем DP
                s_core.hl_buffer[1] &= (vfd_seg_t)~0x80;
            }

            hl_push_buffer_to_ll();
        }
    }

    // --- приоритет: сначала overlay, потом FX ---
    if (display_is_overlay_running()) {
        display_overlay_tick();
    } else {
        display_fx_tick();
    }
}

