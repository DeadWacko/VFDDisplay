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
    FX_FADE_IN,
    FX_FADE_OUT,
    FX_PULSE,
    FX_WAVE,
    FX_GLITCH,
    FX_MATRIX,
    FX_MORPH,
    FX_DISSOLVE,
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
       EFFECT ENGINE STATE
       ======================================================================= */

    volatile bool      fx_active;
    volatile fx_type_t fx_type;

    absolute_time_t fx_start_time;
    uint32_t        fx_duration_ms;     // 0 = infinite
    uint32_t        fx_frame_ms;
    uint32_t        fx_elapsed_ms;
    uint32_t        fx_total_steps;
    uint32_t        fx_current_step;

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
