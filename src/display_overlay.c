#include "display_api.h"
#include "display_ll.h"
#include "display_font.h"

#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * OVERLAY LAYER # СЕЙЧАС НЕ ИСПОЛЬЗУЕТСЯ НИГДЕ. 
 * -------------
 * Перенос boot / WiFi / NTP оверлеев из легаси в отдельный модуль HL.
 *
 * ВАЖНО:
 *  - Работает ТОЛЬКО поверх уже существующего контента.
 *  - На старте оверлея сохраняет текущий LL-буфер сегментов.
 *  - Во время анимации пишет напрямую в LL-буфер (сегменты), не трогая FX.
 *  - По завершении восстанавливает сохранённый LL-буфер.
 *
 * display_process():
 *   if (display_is_overlay_running())
 *       display_overlay_tick();
 *   else
 *       display_fx_tick();
 */

typedef enum {
    OVERLAY_TYPE_NONE = 0,
    OVERLAY_TYPE_BOOT,
    OVERLAY_TYPE_WIFI,
    OVERLAY_TYPE_NTP,
} overlay_type_t;

typedef struct {
    bool            active;
    overlay_type_t  type;

    absolute_time_t last_tick;
    uint32_t        frame_ms;          // период кадров (общий для анимации)

    // шаги анимации
    uint8_t         step;
    uint8_t         loop;

    // сохранённый базовый LL-буфер
    vfd_seg_t       saved_buffer[VFD_MAX_DIGITS];
    uint8_t         saved_digits;
    bool            buffer_valid;
} overlay_state_t;

static overlay_state_t s_ov = {
    .active        = false,
    .type          = OVERLAY_TYPE_NONE,
    .frame_ms      = 150,
    .step          = 0,
    .loop          = 0,
    .saved_digits  = 0,
    .buffer_valid  = false,
};

/* ------------------------ УТИЛИТЫ ------------------------ */

static inline uint8_t ov_get_digits(void)
{
    uint8_t n = display_ll_get_digit_count();
    if (n == 0 || n > VFD_MAX_DIGITS) n = VFD_MAX_DIGITS;
    return n;
}

static void ov_save_base_buffer(void)
{
    if (!display_ll_is_initialized())
        return;

    if (s_ov.buffer_valid)
        return;

    vfd_seg_t *buf = display_ll_get_buffer();
    uint8_t digits = ov_get_digits();

    for (uint8_t i = 0; i < digits; i++) {
        s_ov.saved_buffer[i] = buf[i];
    }
    s_ov.saved_digits = digits;
    s_ov.buffer_valid = true;
}

static void ov_restore_base_buffer(void)
{
    if (!display_ll_is_initialized())
        return;

    if (!s_ov.buffer_valid)
        return;

    vfd_seg_t *buf = display_ll_get_buffer();

    for (uint8_t i = 0; i < s_ov.saved_digits; i++) {
        buf[i] = s_ov.saved_buffer[i];
    }

    s_ov.buffer_valid = false;
}

/* ------------------------ ЗАВЕРШЕНИЕ ------------------------ */

static void ov_finish(void)
{
    ov_restore_base_buffer();

    s_ov.active       = false;
    s_ov.type         = OVERLAY_TYPE_NONE;
    s_ov.step         = 0;
    s_ov.loop         = 0;
    s_ov.frame_ms     = 150;
}

/* ======================== ПУБЛИЧНЫЙ API ======================== */

bool display_is_overlay_running(void)
{
    return s_ov.active;
}

void display_overlay_stop(void)
{
    if (!s_ov.active)
        return;

    ov_finish();
}

/*
 * display_overlay_boot()
 * ----------------------
 * Boot-анимация: 0,1,2,...,9 на всех разрядах.
 * Параметр duration_ms сейчас не используется — как в легаси, где были фиксированные тайминги.
 */
bool display_overlay_boot(uint32_t duration_ms)
{
    (void)duration_ms;

    if (s_ov.active)
        return false;

    ov_save_base_buffer();

    s_ov.type      = OVERLAY_TYPE_BOOT;
    s_ov.active    = true;
    s_ov.step      = 0;
    s_ov.loop      = 0;
    s_ov.frame_ms  = 150;  // ~150 мс на цифру
    s_ov.last_tick = get_absolute_time();

    return true;
}

/*
 * display_overlay_wifi()
 * ----------------------
 * WiFi-анимация: "8888" / "    " / "8888" / ...
 */
bool display_overlay_wifi(uint32_t duration_ms)
{
    (void)duration_ms;

    if (s_ov.active)
        return false;

    ov_save_base_buffer();

    s_ov.type      = OVERLAY_TYPE_WIFI;
    s_ov.active    = true;
    s_ov.step      = 0;
    s_ov.loop      = 0;
    s_ov.frame_ms  = 200;  // немного медленнее мигание
    s_ov.last_tick = get_absolute_time();

    return true;
}

/*
 * display_overlay_ntp()
 * ---------------------
 * NTP-анимация: бегущая "8" по разрядам: 0,1,2,3,2,1 ... (несколько циклов).
 */
bool display_overlay_ntp(uint32_t duration_ms)
{
    (void)duration_ms;

    if (s_ov.active)
        return false;

    ov_save_base_buffer();

    s_ov.type      = OVERLAY_TYPE_NTP;
    s_ov.active    = true;
    s_ov.step      = 0;
    s_ov.loop      = 0;
    s_ov.frame_ms  = 150;
    s_ov.last_tick = get_absolute_time();

    return true;
}

/* ======================== ОСНОВНОЙ TICK ======================== */

/*
 * Внутренний тик оверлея.
 * Вызывать из display_process(), если display_is_overlay_running() == true.
 */
void display_overlay_tick(void)
{
    if (!s_ov.active)
        return;

    absolute_time_t now = get_absolute_time();
    uint32_t now_ms  = to_ms_since_boot(now);
    uint32_t last_ms = to_ms_since_boot(s_ov.last_tick);

    if (now_ms - last_ms < s_ov.frame_ms)
        return;

    s_ov.last_tick = now;

    vfd_seg_t *buf = display_ll_get_buffer();
    uint8_t digits = ov_get_digits();
    if (digits == 0)
        return;

    switch (s_ov.type) {

    case OVERLAY_TYPE_BOOT:
    {
        // 10 шагов (0..9), потом завершение
        if (s_ov.step >= 10) {
            ov_finish();
            return;
        }

        uint8_t d = (uint8_t)(s_ov.step % 10);
        vfd_seg_t code = g_display_font_digits[d];

        for (uint8_t i = 0; i < digits; i++) {
            buf[i] = code;
        }

        s_ov.step++;
        break;
    }

    case OVERLAY_TYPE_WIFI:
    {
        // Мигание "8888" / пусто, 10 шагов
        if (s_ov.step >= 10) {
            ov_finish();
            return;
        }

        bool on = ((s_ov.step & 1u) == 0);
        vfd_seg_t code_on  = g_display_font_digits[8];
        vfd_seg_t code_off = 0;

        for (uint8_t i = 0; i < digits; i++) {
            buf[i] = on ? code_on : code_off;
        }

        s_ov.step++;
        break;
    }

    case OVERLAY_TYPE_NTP:
    {
        // Паттерн 0,1,2,3,2,1; несколько циклов
        static const uint8_t pattern[] = {0, 1, 2, 3, 2, 1};
        const uint8_t PATTERN_LEN      = (uint8_t)(sizeof(pattern) / sizeof(pattern[0]));
        const uint8_t LOOPS            = 3;

        if (s_ov.loop >= LOOPS) {
            ov_finish();
            return;
        }

        if (s_ov.step >= PATTERN_LEN) {
            s_ov.step = 0;
            s_ov.loop++;
            if (s_ov.loop >= LOOPS) {
                ov_finish();
                return;
            }
        }

        uint8_t pos = pattern[s_ov.step];

        vfd_seg_t code_8 = g_display_font_digits[8];

        for (uint8_t i = 0; i < digits; i++) {
            buf[i] = 0;
        }

        if (pos < digits) {
            buf[pos] = code_8;
        }

        s_ov.step++;
        break;
    }

    case OVERLAY_TYPE_NONE:
    default:
        ov_finish();
        break;
    }
}
