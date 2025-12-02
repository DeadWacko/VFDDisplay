#include "display_api.h"
#include "display_ll.h"
#include "display_font.h"
#include "display_state.h"

#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * OVERLAY LAYER
 * -------------
 * Реализация временных уведомлений (Boot, WiFi, NTP),
 * которые имеют высший приоритет над контентом и эффектами.
 *
 * Архитектура v1.0:
 * - Состояние хранится в g_display.
 * - Используются безопасные методы LL (set_digit_raw).
 * - Логика snapshot/restore реализована через saved_buffer в g_display.
 */

// ============================================================================
//  Вспомогательные функции
// ============================================================================

/* Создание снимка экрана перед запуском оверлея */
static void ov_save_snapshot(void)
{
    if (!display_ll_is_initialized()) return;
    
    // Если уже есть сохраненный буфер (например, от FX), не перезаписываем его,
    // чтобы не потерять исходный контент.
    if (g_display->saved_valid) return;

    vfd_segment_map_t *ll_buf = display_ll_get_buffer(); // Только чтение
    uint8_t digits = g_display->digit_count;

    for (uint8_t i = 0; i < digits; i++) {
        g_display->saved_content_buffer[i] = ll_buf[i];
        // Яркость тоже можно сохранить, если оверлей ее меняет
    }
    g_display->saved_valid = true;
}

/* Восстановление экрана после завершения */
static void ov_restore_snapshot(void)
{
    if (!display_ll_is_initialized()) return;
    if (!g_display->saved_valid) return;

    uint8_t digits = g_display->digit_count;
    for (uint8_t i = 0; i < digits; i++) {
        display_ll_set_digit_raw(i, g_display->saved_content_buffer[i]);
    }
    g_display->saved_valid = false;
}

/* Завершение работы оверлея */
/* 
 * Завершение работы оверлея.
 * FIX #20: Сохраняем тип оверлея перед сбросом, чтобы передать его в колбэк.
 */
static void ov_finish(void)
{
    ov_restore_snapshot();

    // 1. Сохраняем тип перед очисткой
    overlay_type_t finished_type = g_display->ov_type;

    // 2. Сбрасываем состояние
    g_display->ov_active = false;
    g_display->ov_type   = OV_NONE;
    
    // 3. Вызываем колбэк с реальным типом
    if (g_display->on_overlay_finished) {
        g_display->on_overlay_finished(finished_type);
    }
}

// ============================================================================
//  Публичный API
// ============================================================================

bool display_is_overlay_running(void)
{
    return g_display->ov_active;
}

void display_overlay_stop(void)
{
    if (!g_display->ov_active) return;
    ov_finish();
}

static bool overlay_start_common(overlay_type_t type, uint32_t frame_ms)
{
    if (!g_display->initialized) return false;
    if (g_display->ov_active) return false; // Не прерываем текущий оверлей

    ov_save_snapshot();

    g_display->ov_type      = type;
    g_display->ov_active    = true;
    g_display->ov_step      = 0;
    g_display->ov_loop      = 0;
    g_display->ov_frame_ms  = frame_ms;
    g_display->ov_start_time = get_absolute_time(); // Используем как last_tick

    return true;
}

bool display_overlay_boot(uint32_t duration_ms)
{
    (void)duration_ms; // Игнорируем, используем фиксированную логику
    return overlay_start_common(OV_BOOT, 150);
}

bool display_overlay_wifi(uint32_t duration_ms)
{
    (void)duration_ms;
    return overlay_start_common(OV_WIFI, 200);
}

bool display_overlay_ntp(uint32_t duration_ms)
{
    (void)duration_ms;
    return overlay_start_common(OV_NTP, 150);
}

// ============================================================================
//  Логика обновления (Tick)
// ============================================================================

void display_overlay_tick(void)
{
    if (!g_display->ov_active) return;

    absolute_time_t now = get_absolute_time();
    uint32_t now_ms  = to_ms_since_boot(now);
    uint32_t last_ms = to_ms_since_boot(g_display->ov_start_time); // Здесь храним last_tick

    if (now_ms - last_ms < g_display->ov_frame_ms) return;

    // Обновляем метку времени
    g_display->ov_start_time = now; 

    uint8_t digits = g_display->digit_count;

    switch (g_display->ov_type) {

    /* BOOT: Перебор цифр 0..9 на всех разрядах */
    case OV_BOOT:
    {
        if (g_display->ov_step >= 10) {
            ov_finish();
            return;
        }

        uint8_t d = (uint8_t)(g_display->ov_step % 10);
        vfd_segment_map_t code = display_font_digit(d); // Используем безопасный геттер

        for (uint8_t i = 0; i < digits; i++) {
            display_ll_set_digit_raw(i, code);
        }

        g_display->ov_step++;
        break;
    }

    /* WIFI: Мигание "8888" */
    case OV_WIFI:
    {
        if (g_display->ov_step >= 10) { // 5 миганий
            ov_finish();
            return;
        }

        bool on = ((g_display->ov_step & 1u) == 0);
        vfd_segment_map_t code = on ? display_font_digit(8) : 0;

        for (uint8_t i = 0; i < digits; i++) {
            display_ll_set_digit_raw(i, code);
        }

        g_display->ov_step++;
        break;
    }

    /* NTP: Бегущая восьмерка (Snake) */
    case OV_NTP:
    {
        // Паттерн движения
        static const uint8_t pattern[] = {0, 1, 2, 3, 2, 1};
        const uint8_t PATTERN_LEN = 6; 
        const uint8_t LOOPS = 3;

        if (g_display->ov_loop >= LOOPS) {
            ov_finish();
            return;
        }

        if (g_display->ov_step >= PATTERN_LEN) {
            g_display->ov_step = 0;
            g_display->ov_loop++;
            if (g_display->ov_loop >= LOOPS) {
                ov_finish();
                return;
            }
        }

        // Очищаем экран
        for (uint8_t i = 0; i < digits; i++) display_ll_set_digit_raw(i, 0);

        // Рисуем бегущий сегмент
        uint8_t pos = pattern[g_display->ov_step];
        if (pos < digits) {
            display_ll_set_digit_raw(pos, display_font_digit(8));
        }

        g_display->ov_step++;
        break;
    }

    case OV_NONE:
    default:
        ov_finish();
        break;
    }
}