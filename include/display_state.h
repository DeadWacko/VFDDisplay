#ifndef DISPLAY_STATE_H
#define DISPLAY_STATE_H

#include "pico/types.h"
#include "display_ll.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
   КОДЫ ОШИБОК
   ========================================================================== */

typedef enum {
    DISPLAY_ERROR_OK = 0,
    DISPLAY_ERROR_NOT_INIT,
    DISPLAY_ERROR_BUSY,
    DISPLAY_ERROR_INVALID_PARAM,
    DISPLAY_ERROR_NO_MEMORY,
} display_error_t;

/* ============================================================================
   ТИПЫ ЭФФЕКТОВ (FX)
   ========================================================================== */

typedef enum {
    FX_NONE = 0,
    
    // Прозрачные (Intensity Effects) - не блокируют контент
    FX_FADE_IN,
    FX_FADE_OUT,
    FX_PULSE,
    FX_WAVE,
    FX_MATRIX,    // Scanner (KITT)
    FX_HEARTBEAT, // New
    
    // Блокирующие (Structural Effects) - захватывают сегменты
    FX_GLITCH,
    FX_MORPH,
    FX_DISSOLVE,
    FX_SLOT_MACHINE,
    FX_DECODE,
    FX_PINGPONG,
    
    // Текстовые (Text Effects)
    FX_MARQUEE,
    FX_SLIDE_IN
} fx_type_t;

/* ============================================================================
   ТИПЫ ОВЕРЛЕЕВ (OVERLAYS)
   ========================================================================== */

typedef enum {
    OV_NONE = 0,
    OV_BOOT,
    OV_WIFI,
    OV_NTP,
} overlay_type_t;

#define FX_TEXT_MAX_LEN 64 // Макс. длина строки для Marquee/SlideIn

/* ============================================================================
   ГЛОБАЛЬНАЯ СТРУКТУРА СОСТОЯНИЯ
   Хранит все параметры дисплея, буферы и состояние анимаций.
   ========================================================================== */

typedef struct display_state_s {

    /* --- Статус --- */
    volatile bool initialized;         // Флаг инициализации драйвера

    /* --- Геометрия --- */
    uint8_t  digit_count;              // Количество разрядов
    uint16_t refresh_rate_hz;          // Частота обновления

    /* --- Режим работы --- */
    volatile display_mode_t mode;      // Content / Effect / Overlay

    /* --- Буферы контента --- */
    vfd_seg_t content_buffer[VFD_MAX_DIGITS];       // Основной буфер (Content)
    uint8_t   content_brightness[VFD_MAX_DIGITS];   // Базовая яркость

    /* --- Snapshot (Сохраненное состояние перед FX) --- */
    vfd_seg_t saved_content_buffer[VFD_MAX_DIGITS];
    uint8_t   saved_brightness[VFD_MAX_DIGITS];
    bool      saved_valid;

    /* --- Финальный буфер (отправляется в LL) --- */
    volatile uint8_t final_brightness[VFD_MAX_DIGITS];

    /* =========================================================================
       ДВИЖОК ЭФФЕКТОВ (FX ENGINE)
       ======================================================================= */

    volatile bool      fx_active;
    volatile fx_type_t fx_type;

    absolute_time_t fx_start_time;
    uint32_t        fx_duration_ms;
    uint32_t        fx_frame_ms;
    uint32_t        fx_elapsed_ms;
    uint32_t        fx_total_steps;
    uint32_t        fx_current_step;

    uint8_t         fx_base_brightness;

    /* Параметры конкретных эффектов */
    
    // Glitch
    bool            fx_glitch_active;
    uint32_t        fx_glitch_last_ms;
    uint32_t        fx_glitch_next_ms;
    uint32_t        fx_glitch_step;
    uint8_t         fx_glitch_digit;
    uint8_t         fx_glitch_bit;
    vfd_seg_t       fx_glitch_saved_digit;

    // Matrix / Scanner
    uint32_t        fx_matrix_last_ms;
    uint32_t        fx_matrix_step;
    uint32_t        fx_matrix_total_steps;
    uint8_t         fx_matrix_min_percent;
    uint8_t         fx_matrix_brightness_percent[VFD_MAX_DIGITS];

    // Morph
    vfd_seg_t       fx_morph_start[VFD_MAX_DIGITS];
    vfd_seg_t       fx_morph_target[VFD_MAX_DIGITS];
    uint32_t        fx_morph_step;
    uint32_t        fx_morph_steps;

    // Dissolve
    uint8_t         fx_dissolve_order[VFD_MAX_DIGITS * 8];
    uint32_t        fx_dissolve_total_bits;
    uint32_t        fx_dissolve_step;

    // Slot Machine / Decode / Target-based FX
    vfd_seg_t       fx_target_buffer[VFD_MAX_DIGITS];
    uint32_t        fx_stage_step;

    // Ping Pong
    int32_t         fx_pingpong_pos;
    bool            fx_pingpong_dir;

    // Text FX
    char            fx_text_buffer[FX_TEXT_MAX_LEN];
    uint16_t        fx_text_len;

    /* =========================================================================
       ДВИЖОК ОВЕРЛЕЕВ (OVERLAYS)
       ======================================================================= */

    volatile bool           ov_active;
    volatile overlay_type_t ov_type;

    absolute_time_t ov_start_time;
    uint32_t        ov_duration_ms;
    uint32_t        ov_frame_ms;
    uint32_t        ov_step;
    uint32_t        ov_loop;

    /* =========================================================================
       УПРАВЛЕНИЕ ЯРКОСТЬЮ (SYSTEM)
       ======================================================================= */

    volatile bool auto_brightness_enabled;
    volatile bool night_mode_enabled;

    uint8_t user_brightness_level;
    uint8_t night_brightness;
    uint8_t night_start_hour;
    uint8_t night_end_hour;
    
    uint16_t adc_pin;
    absolute_time_t brightness_last_update;
    uint32_t brightness_update_period_ms;

    /* =========================================================================
       ИНДИКАЦИЯ (DOTS)
       ======================================================================= */

    volatile bool dot_blink_enabled;
    volatile bool dot_state;
    uint32_t      dot_period_ms;
    absolute_time_t dot_last_toggle;

    uint8_t dot_digit_positions[2];
    uint8_t dot_bit;

    /* =========================================================================
       CALLBACKS
       ======================================================================= */

    void (*on_effect_finished)(fx_type_t type);
    void (*on_overlay_finished)(overlay_type_t type);

} display_state_t;

/* Глобальный указатель на экземпляр состояния (Singleton) */
extern display_state_t *const g_display;

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_STATE_H