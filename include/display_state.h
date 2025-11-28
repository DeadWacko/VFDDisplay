#ifndef DISPLAY_STATE_H
#define DISPLAY_STATE_H

#include "pico/types.h"
#include "display_ll.h"
#include <stdbool.h>

/*
 * ВАЖНО:
 *  - Тип display_mode_t определяется в display_api.h.
 *  - Здесь мы его только используем.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
   HIGH-LEVEL GLOBAL ERROR CODES
   ========================================================================== */

typedef enum {
    DISPLAY_ERROR_OK = 0,
    DISPLAY_ERROR_NOT_INIT,
    DISPLAY_ERROR_BUSY,
    DISPLAY_ERROR_INVALID_PARAM,
    DISPLAY_ERROR_NO_MEMORY,
} display_error_t;

/* ============================================================================
   FX types (one at a time)
   ========================================================================== */

typedef enum {
    FX_NONE = 0,
    // Яркостные (Прозрачные)
    FX_FADE_IN,
    FX_FADE_OUT,
    FX_PULSE,
    FX_WAVE,
    FX_MATRIX,    // Scanner
    
    // Структурные (Блокирующие)
    FX_GLITCH,
    FX_MORPH,
    FX_DISSOLVE,
    
    // Новые текстовые
    FX_MARQUEE,   // Бегущая строка
    FX_SLIDE_IN   // Заезд текста
} fx_type_t;




/* ============================================================================
   Overlay types (one at a time)
   ========================================================================== */

typedef enum {
    OV_NONE = 0,
    OV_BOOT,
    OV_WIFI,
    OV_NTP,
} overlay_type_t;


#define FX_TEXT_MAX_LEN 64 // Максимальная длина бегущей строки

/* ============================================================================
   MAIN STATE STRUCTURE
   — the only global HL state
   — stored as one static instance in display_core.c
   — accessed ONLY via g_display pointer
   ========================================================================== */

typedef struct display_state_s {

    /* === Initialization === */
    volatile bool initialized;         // HL+LL fully ready

    /* === Display geometry === */
    uint8_t  digit_count;              // 1..10 (copy of LL config)
    uint16_t refresh_rate_hz;          // cached for calculations

    /* === Current mode (enum из display_api.h) === */
    volatile display_mode_t mode;

    /* === CONTENT BUFFERS (application → HL) === */
    vfd_seg_t content_buffer[VFD_MAX_DIGITS];       // raw content segments
    uint8_t   content_brightness[VFD_MAX_DIGITS];   // base per-digit brightness

    /* === Saved buffers for FX/OVERLAY === */
    vfd_seg_t saved_content_buffer[VFD_MAX_DIGITS];
    uint8_t   saved_brightness[VFD_MAX_DIGITS];
    bool      saved_valid;

    /* === Final brightness (HL → LL after auto/night adjustments) === */
    volatile uint8_t final_brightness[VFD_MAX_DIGITS];

    /* =========================================================================
       EFFECT ENGINE STATE (one effect at a time)
       ======================================================================= */

    volatile bool      fx_active;          // сейчас запущен какой-то эффект
    volatile fx_type_t fx_type;            // тип эффекта

    // Общее время/шаги для всех FX
    absolute_time_t fx_start_time;         // когда стартовали (absolute_time_t)
    uint32_t        fx_duration_ms;        // общая длительность эффекта, 0 = infinite
    uint32_t        fx_frame_ms;           // базовый шаг (кадр) для step-эффектов
    uint32_t        fx_elapsed_ms;         // последний посчитанный elapsed (для отладки)
    uint32_t        fx_total_steps;        // duration_ms / frame_ms (или спец. значение)
    uint32_t        fx_current_step;       // 0..fx_total_steps-1

    // Базовая яркость до запуска эффекта (для восстановления)
    uint8_t         fx_base_brightness;

    /* ---- GLITCH (динамический фликер) ------------------------------------- */

    bool            fx_glitch_active;      // идёт ли сейчас burst фликера
    uint32_t        fx_glitch_last_ms;     // последний тик внутри burst
    uint32_t        fx_glitch_next_ms;     // когда стартовать следующий burst (от начала FX)
    uint32_t        fx_glitch_step;        // шаг внутри паттерна
    uint8_t         fx_glitch_digit;       // какой разряд трогаем
    uint8_t         fx_glitch_bit;         // какой бит (0..6), DP не трогаем
    vfd_seg_t       fx_glitch_saved_digit; // исходная маска этого разряда

    /* ---- MATRIX RAIN ------------------------------------------------------ */

    uint32_t        fx_matrix_last_ms;                       // последний кадр matrix
    uint32_t        fx_matrix_step;                          // счётчик шагов
    uint32_t        fx_matrix_total_steps;                   // всего шагов
    uint8_t         fx_matrix_min_percent;                   // минимальный % яркости
    uint8_t         fx_matrix_brightness_percent[VFD_MAX_DIGITS]; // 0..100 на разряд

    /* ---- MORPH (плавный переход цифра→цифра) ------------------------------ */

    vfd_seg_t       fx_morph_start[VFD_MAX_DIGITS];          // стартовые сегменты
    vfd_seg_t       fx_morph_target[VFD_MAX_DIGITS];         // целевые сегменты
    uint32_t        fx_morph_step;                           // текущий шаг
    uint32_t        fx_morph_steps;                          // всего шагов

    /* ---- DISSOLVE (рассыпающиеся сегменты) -------------------------------- */

    uint8_t         fx_dissolve_order[VFD_MAX_DIGITS * 8];   // порядок выключения битов
    uint32_t        fx_dissolve_total_bits;                  // всего битов, задействованных в FX
    uint32_t        fx_dissolve_step;                        // текущий шаг по dissolve_order[]




    /* ---- НОВЫЕ ПОЛЯ ---- */
    
    // Общий буфер цели для эффектов, которым нужно к чему-то прийти (Slot, Decode)
    vfd_seg_t       fx_target_buffer[VFD_MAX_DIGITS];

    // Slot Machine & Decode
    uint32_t        fx_stage_step;    // Какой разряд сейчас "останавливается"

    // Ping Pong
    int32_t         fx_pingpong_pos;  // Текущая позиция (может быть float logic внутри)
    bool            fx_pingpong_dir;  // Направление


    /* =========================================================================
       OVERLAY ENGINE STATE
       ======================================================================= */

    volatile bool         ov_active;
    volatile overlay_type_t ov_type;

    absolute_time_t ov_start_time;
    uint32_t        ov_duration_ms;     // 0 = infinite
    uint32_t        ov_frame_ms;
    uint32_t        ov_step;
    uint32_t        ov_loop;

    /* =========================================================================
       BRIGHTNESS CONTROL (auto + night)
       ======================================================================= */

    volatile bool auto_brightness_enabled;
    volatile bool night_mode_enabled;

    uint8_t user_brightness_level;    // base 0..255
    uint8_t night_brightness;         // e.g. 10
    uint8_t night_start_hour;         // e.g. 23
    uint8_t night_end_hour;           // e.g. 7
    uint16_t adc_pin;                 // light sensor ADC
    absolute_time_t brightness_last_update;
    uint32_t brightness_update_period_ms;

    /* =========================================================================
       BLINKING DOT / COLON
       ======================================================================= */

    volatile bool dot_blink_enabled;
    volatile bool dot_state;
    uint32_t      dot_period_ms;
    absolute_time_t dot_last_toggle;

    uint8_t dot_digit_positions[2];    // e.g. {1, 2}
    uint8_t dot_bit;                   // usually bit 7

    /* =========================================================================
       CALLBACKS (optional, may be NULL)
       ======================================================================= */

    void (*on_effect_finished)(fx_type_t type);
    void (*on_overlay_finished)(overlay_type_t type);

    // Буфер для текстовых эффектов
    char     fx_text_buffer[FX_TEXT_MAX_LEN];
    uint16_t fx_text_len;

} display_state_t;

/* ============================================================================
   GLOBAL POINTER (only one)
   initialized in display_core.c
   ========================================================================== */

extern display_state_t *const g_display;

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_STATE_H
