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

/* ============================================================================
 *  Единственный экземпляр HL state
 * ========================================================================== */

static display_state_t g_display_state;
display_state_t *const g_display = &g_display_state;

/* ============================================================================
 *  Внутренние настройки по умолчанию (пока жёстко зашиты)
 *  Потом, когда появится display_config_t, всё это уедет в конфиг
 * ========================================================================== */

#define DISPLAY_DEFAULT_DATA_PIN      15
#define DISPLAY_DEFAULT_CLOCK_PIN     14
#define DISPLAY_DEFAULT_LATCH_PIN     13
#define DISPLAY_DEFAULT_REFRESH_HZ    120

#define DISPLAY_DEFAULT_ADC_PIN       26   // GPIO для фоторезистора (ADC0)
#define DISPLAY_BRIGHTNESS_MIN_AUTO   5    // минимальная яркость в авто-режиме
#define DISPLAY_BRIGHTNESS_NIGHT      10   // ночная яркость

#define DISPLAY_BRIGHTNESS_UPDATE_MS  1000 // раз в секунду
#define DISPLAY_BRIGHTNESS_HYSTERESIS 2    // не дёргаем, если изменение < 2 уровней

#define DOT_DEFAULT_PERIOD_MS         1000 // 1 Гц мигания
#define DOT_DEFAULT_BIT               7    // DP = бит 7
#define DOT_DEFAULT_POS1              1    // второй разряд (с нуля)
#define DOT_DEFAULT_POS2              2    // третий (если нужно)

/* ============================================================================
 *  Вспомогательные функции
 * ========================================================================== */

static inline uint8_t clamp_u8(uint32_t v, uint8_t max_val)
{
    if (v > max_val) return max_val;
    return (uint8_t)v;
}

/* Атомарная заливка яркости в LL (через LL-API, внутри уже есть защита) */
static void core_push_brightness_to_ll(uint8_t level)
{
    if (!display_ll_is_initialized()) return;
    display_ll_set_brightness_all(level);
}

/* Пушим текущий CONTENT-буфер (с учётом точки) в LL */
static void core_push_content_to_ll(void)
{
    if (!display_ll_is_initialized() || !g_display->initialized)
        return;

    uint8_t digits = g_display->digit_count;
    if (digits == 0 || digits > VFD_MAX_DIGITS)
        digits = VFD_MAX_DIGITS;

    for (uint8_t i = 0; i < digits; i++) {
        vfd_seg_t seg = g_display->content_buffer[i];

        // Если мигание точки активно и мы в состоянии "точка включена" — добавим DP-бит
        if (g_display->dot_blink_enabled && g_display->dot_state) {
            for (uint8_t k = 0; k < 2; k++) {
                if (g_display->dot_digit_positions[k] < digits &&
                    g_display->dot_digit_positions[k] == i) {
                    seg |= (vfd_seg_t)(1u << g_display->dot_bit);
                }
            }
        }

        display_ll_set_digit_raw(i, seg);
    }

    // Яркость уже залита отдельной функцией (core_update_brightness)
}

/* Среднее по нескольким чтениям АЦП для фильтра по освещённости */
static uint16_t core_read_adc_filtered(uint16_t adc_pin)
{
    // Предполагаем GPIO 26..29 → ADC 0..3
    uint16_t input = 0;
    if (adc_pin >= 26 && adc_pin <= 29) {
        input = (uint16_t)(adc_pin - 26);
    }

    adc_select_input(input);

    const uint8_t samples = 8;
    uint32_t acc = 0;
    for (uint8_t i = 0; i < samples; i++) {
        acc += adc_read();  // 0..4095
    }
    uint16_t avg = (uint16_t)(acc / samples);
    return avg;
}

/* Пересчитать итоговую яркость (учитывая авто/ночной режим) */
static void core_update_brightness_now(void)
{
    uint8_t new_level = g_display->user_brightness_level;

    if (g_display->auto_brightness_enabled) {
        // Автояркость по датчику
        uint16_t raw = core_read_adc_filtered((uint16_t)g_display->adc_pin); // 0..4095
        uint32_t val = (uint32_t)raw * VFD_MAX_BRIGHTNESS;
        uint8_t level = (uint8_t)(val / 4095u);

        if (level < DISPLAY_BRIGHTNESS_MIN_AUTO) {
            level = DISPLAY_BRIGHTNESS_MIN_AUTO;
        }
        new_level = level;
    } else if (g_display->night_mode_enabled) {
        // Ночной режим по времени
        if (rtc_running()) {
            datetime_t dt;
            rtc_get_datetime(&dt);

            bool is_night = false;
            uint8_t start = g_display->night_start_hour;
            uint8_t end   = g_display->night_end_hour;

            if (start == end) {
                // странный случай, считаем ночь отключённой
                is_night = false;
            } else if (start < end) {
                // без перехода через полночь
                is_night = (dt.hour >= start && dt.hour < end);
            } else {
                // с переходом через полночь (напр. 23..7)
                is_night = (dt.hour >= start || dt.hour < end);
            }

            if (is_night) {
                new_level = g_display->night_brightness;
            } else {
                new_level = g_display->user_brightness_level;
            }
        } else {
            // RTC не настроены → просто используем пользовательскую яркость
            new_level = g_display->user_brightness_level;
        }
    } else {
        // Ручной режим
        new_level = g_display->user_brightness_level;
    }

    if (new_level > VFD_MAX_BRIGHTNESS) {
        new_level = VFD_MAX_BRIGHTNESS;
    }

    // Гистерезис: берём текущий «реальный» уровень из final_brightness[0]
    uint8_t current_level = g_display->final_brightness[0];
    uint8_t diff = (current_level > new_level)
                 ? (current_level - new_level)
                 : (new_level - current_level);

    if (diff < DISPLAY_BRIGHTNESS_HYSTERESIS) {
        // Разница слишком мала — не обновляем, чтобы не мёрцало
        return;
    }

    // Обновляем массив final_brightness и пушим в LL
    for (uint8_t i = 0; i < g_display->digit_count; i++) {
        g_display->final_brightness[i] = new_level;
    }
    core_push_brightness_to_ll(new_level);
}

/* Периодическое обновление яркости (по таймеру) */
static void core_brightness_tick(absolute_time_t now)
{
    // Если запущен FX или Overlay — brightness logic запрещена
    if (g_display->fx_active || g_display->ov_active) {
        return;
    }

    if (!(g_display->auto_brightness_enabled || g_display->night_mode_enabled))
        return;

    uint32_t now_ms  = to_ms_since_boot(now);
    uint32_t last_ms = to_ms_since_boot(g_display->brightness_last_update);

    if (now_ms - last_ms < g_display->brightness_update_period_ms)
        return;

    g_display->brightness_last_update = now;
    core_update_brightness_now();
}


/* Мигание точки (колонки и бит задаются в g_display) */
static void core_dot_blink_tick(absolute_time_t now)
{
    if (!g_display->dot_blink_enabled)
        return;
    if (g_display->digit_count == 0)
        return;

    // Точку мы включаем только когда нет overlay/эффекта,
    // чтобы не лезть поверх анимаций
    if (g_display->ov_active || g_display->fx_active)
        return;

    uint32_t now_ms  = to_ms_since_boot(now);
    uint32_t last_ms = to_ms_since_boot(g_display->dot_last_toggle);

    if (now_ms - last_ms < g_display->dot_period_ms)
        return;

    g_display->dot_last_toggle = now;
    g_display->dot_state = !g_display->dot_state;

    // При каждом переключении просто перепушиваем контент → LL
    core_push_content_to_ll();
}

/* ============================================================================
 *  Публичный API (из display_api.h)
 *  Пока оставляем сигнатуры как есть, чтобы не ломать существующий код.
 *  Позже (по ТЗ) обновим до display_config_t + display_error_t.
 * ========================================================================== */

void display_init(uint8_t digit_count)
{
    if (digit_count == 0 || digit_count > VFD_MAX_DIGITS)
        digit_count = 4;

    memset(&g_display_state, 0, sizeof(g_display_state));

    g_display->digit_count       = digit_count;
    g_display->refresh_rate_hz   = DISPLAY_DEFAULT_REFRESH_HZ;
    g_display->mode              = DISPLAY_MODE_CONTENT;

    // Базовые уровни яркости
    g_display->user_brightness_level   = VFD_MAX_BRIGHTNESS;
    g_display->night_brightness        = DISPLAY_BRIGHTNESS_NIGHT;
    g_display->auto_brightness_enabled = false;
    g_display->night_mode_enabled      = false;

    // Ночной интервал по умолчанию — как в старом коде: 23..7
    g_display->night_start_hour = 23;
    g_display->night_end_hour   = 7;

    // ADC / автояркость
    g_display->adc_pin                    = DISPLAY_DEFAULT_ADC_PIN;
    g_display->brightness_update_period_ms = DISPLAY_BRIGHTNESS_UPDATE_MS;
    g_display->brightness_last_update      = get_absolute_time();

    // Мигающая точка
    g_display->dot_blink_enabled  = false;
    g_display->dot_state          = false;
    g_display->dot_period_ms      = DOT_DEFAULT_PERIOD_MS;
    g_display->dot_last_toggle    = get_absolute_time();
    g_display->dot_digit_positions[0] = DOT_DEFAULT_POS1;
    g_display->dot_digit_positions[1] = DOT_DEFAULT_POS2;
    g_display->dot_bit            = DOT_DEFAULT_BIT;

    // Колбэки пока не задействованы (будут использоваться FX/OVERLAY)
    g_display->on_effect_finished  = NULL;
    g_display->on_overlay_finished = NULL;

    // CONTENT/FINAL буферы
    for (uint8_t i = 0; i < VFD_MAX_DIGITS; i++) {
        g_display->content_buffer[i]        = 0;
        g_display->content_brightness[i]    = VFD_MAX_BRIGHTNESS;
        g_display->saved_content_buffer[i]  = 0;
        g_display->saved_brightness[i]      = 0;
        g_display->final_brightness[i]      = VFD_MAX_BRIGHTNESS;
    }
    g_display->saved_valid = false;

    // Инициализация LL
    display_ll_config_t cfg = {
        .data_pin        = DISPLAY_DEFAULT_DATA_PIN,
        .clock_pin       = DISPLAY_DEFAULT_CLOCK_PIN,
        .latch_pin       = DISPLAY_DEFAULT_LATCH_PIN,
        .digit_count     = g_display->digit_count,
        .refresh_rate_hz = g_display->refresh_rate_hz,
    };

    if (!display_ll_init(&cfg)) {
        LOG_ERROR("display_init: LL init failed");
        g_display->initialized = false;
        return;
    }

    if (!display_ll_start_refresh()) {
        LOG_ERROR("display_init: LL start_refresh failed");
        g_display->initialized = false;
        display_ll_deinit();
        return;
    }

    // Инициализация ADC для автояркости (пока всегда, он недорогой)
    adc_init();
    adc_gpio_init((uint)g_display->adc_pin);
    if (g_display->adc_pin >= 26 && g_display->adc_pin <= 29) {
        adc_select_input(g_display->adc_pin - 26);
    }

    // Начальное применение яркости и контента
    core_update_brightness_now();
    core_push_content_to_ll();

    g_display->initialized = true;
    LOG_INFO("display_init: HL+LL initialized, digits=%u", g_display->digit_count);
}

/* Текущий режим отображения */
display_mode_t display_get_mode(void)
{
    if (g_display->ov_active) return DISPLAY_MODE_OVERLAY;
    if (g_display->fx_active) return DISPLAY_MODE_EFFECT;
    return DISPLAY_MODE_CONTENT;
}

/* Идёт ли эффект / оверлей
 * (реально делегируем FX/OVERLAY-модулям, пока их не переписали под g_display)
 */
extern bool display_fx_is_running(void);
extern bool display_is_overlay_running(void);

bool display_is_effect_running(void)
{
    return display_fx_is_running();
}

/* ============================================================================
 *      ЯРКОСТЬ
 * ========================================================================== */

void display_set_brightness(uint8_t brightness)
{
    if (brightness > VFD_MAX_BRIGHTNESS)
        brightness = VFD_MAX_BRIGHTNESS;

    g_display->user_brightness_level = brightness;

    // Ручной режим — сразу применяем
    if (!g_display->auto_brightness_enabled && !g_display->night_mode_enabled) {
        for (uint8_t i = 0; i < g_display->digit_count; i++) {
            g_display->final_brightness[i] = brightness;
        }
        core_push_brightness_to_ll(brightness);
    } else {
        // При активных режимах — просто пересчитаем общую логику
        core_update_brightness_now();
    }
}

void display_set_auto_brightness(bool enable)
{
    g_display->auto_brightness_enabled = enable;
    if (enable) {
        g_display->night_mode_enabled = false; // auto и night одновременно не нужны
    }
    core_update_brightness_now();
}

void display_set_night_mode(bool enable)
{
    g_display->night_mode_enabled = enable;
    if (enable) {
        g_display->auto_brightness_enabled = false;
    }
    core_update_brightness_now();
}

/* ============================================================================
 *      ТОЧКА / ДВОЕТОЧИЕ
 * ========================================================================== */

void display_set_dot_blinking(bool enable)
{
    g_display->dot_blink_enabled = enable;
    g_display->dot_state         = false;
    g_display->dot_last_toggle   = get_absolute_time();

    if (!enable) {
        // При выключении мигания просто перепушим контент (точка погаснет)
        core_push_content_to_ll();
    }
}

/* ============================================================================
 *      ПРИЁМ CONTENT-буфера из display_content.c
 * ========================================================================== */

/*
 * ВАЖНО:
 *  - сюда приходит "чистый" контент (цифры/символы) без учёта мигающей точки;
 *  - функция обновляет HL-буфер и, если нет FX/OVERLAY, сразу пушит в LL;
 *  - DP-бит добавляется в core_push_content_to_ll() в зависимости от dot_state.
 */
void display_core_set_buffer(const vfd_seg_t *buf, uint8_t size)
{
    if (!g_display->initialized || !buf)
        return;

    uint8_t n = size;
    if (n > g_display->digit_count)
        n = g_display->digit_count;

    // Копируем в content_buffer
    for (uint8_t i = 0; i < n; i++) {
        g_display->content_buffer[i] = buf[i];
    }
    for (uint8_t i = n; i < g_display->digit_count; i++) {
        g_display->content_buffer[i] = 0;
    }

    // Если сейчас нет оверлея и эффекта — можно сразу вывести в LL
    if (!display_is_overlay_running() && !display_fx_is_running()) {
        core_push_content_to_ll();
    }
}

/* Доступ к CONTENT-буферу (для отладочных задач / форматирования текста) */
vfd_seg_t *display_content_buffer(void)
{
    return g_display->content_buffer;
}

/* ============================================================================
 *      MAIN PROCESS LOOP
 * ========================================================================== */

/*
 * Главная точка входа HL.
 * Вызывать часто в главном цикле:
 *
 *  while (true) {
 *      display_process();
 *      sleep_ms(2);
 *  }
 *
 * Гарантии:
 *  - overlay > effect > content;
 *  - если нет ни overlay, ни эффекта, то просто обслуживаем точку и яркость;
 *  - тайминги строго на absolute_time_t / to_ms_since_boot().
 */

extern void display_fx_tick(void);
extern void display_overlay_tick(void);

void display_process(void)
{
    if (!g_display->initialized)
        return;

    absolute_time_t now = get_absolute_time();

    // 1) Яркость (auto/night, с фильтром и гистерезисом)
    core_brightness_tick(now);

    // 2) Приоритет: сначала overlay
    if (display_is_overlay_running()) {
        g_display->ov_active = true;
        g_display->fx_active = false;

        display_overlay_tick();
        g_display->mode = DISPLAY_MODE_OVERLAY;
        return;
    } else {
        g_display->ov_active = false;
    }

    // 3) Потом эффект
    if (display_fx_is_running()) {
        g_display->fx_active = true;

        display_fx_tick();
        g_display->mode = DISPLAY_MODE_EFFECT;
        return;
    } else {
        g_display->fx_active = false;
    }

    // 4) Ни overlay, ни эффект → CONTENT
    g_display->mode = DISPLAY_MODE_CONTENT;

    // Мигание точки и перепуш HL→LL при смене её состояния
    core_dot_blink_tick(now);

    // CONTENT сам не меняется во времени, но если вдруг кто-то захочет
    // дергать display_process() после изменения content_buffer вручную —
    // можно гарантированно дотолкать его в LL:
    // (но это стоит дёшево, так что можно делать всегда)
    core_push_content_to_ll();
}
