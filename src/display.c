#include "display_ll.h"
#include "display.h"     // пока старый API остаётся
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "hardware/rtc.h"
#include "hardware/sync.h"
#include "logging.h"


#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define PI_F 3.14159265f

static const uint8_t SINE_TABLE[20] = {
    0, 10, 20, 30, 39, 47, 55, 62, 69, 75,
    80, 85, 89, 92, 94, 95, 94, 92, 89, 85
};

static inline float gamma_correct(float x) {
    // Простая гамма-коррекция ~2.0 — для VFD очень хорошо сглаживает переходы
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return x * x;
}

volatile display_mode_t g_display_mode = DISPLAY_MODE_TIME;
static bool s_flicker_triggered = false; // Статичная, доступ через display_flicker_triggered()

typedef struct {
    uint8_t data_pin;
    uint8_t clock_pin;
    uint8_t latch_pin;
    uint8_t light_sensor_adc;
    uint8_t digit_count;
    uint16_t fast_refresh_rate;
    uint8_t night_start_hour;
    uint8_t night_end_hour;
    void (*on_effect_complete)(void);
    bool night_mode_enabled;
    bool adaptive_brightness_enabled;
    bool manual_brightness_enabled;
    uint8_t user_brightness;
    uint8_t display_brightness;
    uint8_t digit_brightness[DISPLAY_MAX_DIGITS];
    volatile uint8_t display_buffer[DISPLAY_MAX_DIGITS];
    uint8_t current_digit;
    struct repeating_timer fast_timer;
    struct repeating_timer time_timer;
    struct repeating_timer brightness_timer;
    struct repeating_timer dot_timer;
    alarm_id_t display_clear_alarm;
    struct repeating_timer scroll_timer;
    char scroll_str[64];
    int scroll_pos;
    int scroll_len;
    struct repeating_timer morph_timer;
    uint8_t morph_target_buffer[DISPLAY_MAX_DIGITS];
    int morph_step;
    int morph_steps;
    struct repeating_timer boot_timer;
    int boot_step;
    struct repeating_timer wifi_timer;
    int wifi_step;
    struct repeating_timer ntp_timer;
    int ntp_step;
    int ntp_loop;

    struct repeating_timer pulse_timer;
    int pulse_step;
    int pulse_cycles;
    int pulse_total_steps;

    struct repeating_timer fade_out_timer;
    int fade_out_step;
    int fade_out_total_steps;

    struct repeating_timer fade_in_timer;
    int fade_in_step;
    int fade_in_total_steps;

    struct repeating_timer wave_timer;
    int wave_step;
    int wave_total_steps;

    struct repeating_timer flicker_timer;
    uint32_t flicker_min_interval_ms;
    uint32_t flicker_max_interval_ms;
    uint32_t flicker_duration_ms;
    int flicker_step;
    int flicker_digit;
    int flicker_bit;
    uint8_t flicker_original_buffer[DISPLAY_MAX_DIGITS];
    int flicker_count;
    bool dot_blink_flag;
    bool dot_blinking_active;
    bool time_timer_active;
    bool brightness_timer_active;
    bool dot_timer_active;
    bool scroll_timer_active;
    bool morph_timer_active;
    bool boot_timer_active;
    bool wifi_timer_active;
    bool ntp_timer_active;
    bool pulse_timer_active;
    bool fade_out_timer_active;
    bool fade_in_timer_active;
    bool wave_timer_active;
    bool flicker_timer_active;

    // Сохранение базового содержимого для оверлеев (boot/wifi/ntp)
    uint8_t overlay_saved_buffer[DISPLAY_MAX_DIGITS];
    bool overlay_buffer_valid;
} display_state_t;

static display_state_t s_state = {
    .display_brightness = DISPLAY_BRIGHTNESS_MAX,
    .user_brightness = DISPLAY_BRIGHTNESS_MAX,
    .current_digit = 0,
    .display_clear_alarm = 0,
    .scroll_pos = 0,
    .scroll_len = 0,
    .morph_step = 0,
    .morph_steps = 0,
    .boot_step = 0,
    .wifi_step = 0,
    .ntp_step = 0,
    .ntp_loop = 0,

    .pulse_step = 0,
    .pulse_cycles = 0,
    .pulse_total_steps = 0,

    .fade_out_step = 0,
    .fade_out_total_steps = 0,

    .fade_in_step = 0,
    .fade_in_total_steps = 0,

    .wave_step = 0,
    .wave_total_steps = 0,

    .flicker_min_interval_ms = 0,
    .flicker_max_interval_ms = 0,
    .flicker_duration_ms = 0,
    .flicker_step = 0,
    .flicker_digit = -1,
    .flicker_bit = -1,
    .flicker_count = 0,
    .dot_blink_flag = false,
    .dot_blinking_active = false,
    .time_timer_active = false,
    .brightness_timer_active = false,
    .dot_timer_active = false,
    .scroll_timer_active = false,
    .morph_timer_active = false,
    .boot_timer_active = false,
    .wifi_timer_active = false,
    .ntp_timer_active = false,
    .pulse_timer_active = false,
    .fade_out_timer_active = false,
    .fade_in_timer_active = false,
    .wave_timer_active = false,
    .flicker_timer_active = false,
    .night_mode_enabled = false,
    .adaptive_brightness_enabled = false,
    .manual_brightness_enabled = false,
    .overlay_buffer_valid = false
};

static spin_lock_t *s_lock;
static bool s_display_active = false;

// --- Matrix rain effect state (separate from s_state) ---
static struct repeating_timer s_matrix_timer;
static bool s_matrix_timer_active = false;
static int s_matrix_step = 0;
static int s_matrix_total_steps = 0;
static uint8_t s_matrix_min_brightness = 5;

// --- Helper: save/restore base display buffer for overlay effects (boot/wifi/ntp) ---
static void overlay_save_base_buffer(void) {
    if (!s_display_active) return;

    uint32_t flags = spin_lock_blocking(s_lock);
    if (!s_state.overlay_buffer_valid) {
        for (int i = 0; i < s_state.digit_count; i++) {
            s_state.overlay_saved_buffer[i] = s_state.display_buffer[i];
        }
        s_state.overlay_buffer_valid = true;
    }
    spin_unlock(s_lock, flags);
}

static void overlay_restore_base_buffer(void) {
    if (!s_display_active) return;

    uint32_t flags = spin_lock_blocking(s_lock);
    if (s_state.overlay_buffer_valid) {
        for (int i = 0; i < s_state.digit_count; i++) {
            s_state.display_buffer[i] = s_state.overlay_saved_buffer[i];
        }
        s_state.overlay_buffer_valid = false;
    }
    spin_unlock(s_lock, flags);
}


static const uint8_t SEGMENT_CODES[15] = {
    0b01111011, 0b00000011, 0b01011110, 0b01001111, 0b00100111,
    0b01101101, 0b01111101, 0b01000011, 0b01111111, 0b01101111,
    0b00000000, 0b10000000, 0b01110100, 0b00000100, 0b00000010  // '-' на позиции 14
};

static void update_final_brightness(void);
static void shift_out(uint8_t data);
static void latch(void);
static int64_t display_clear_cb(alarm_id_t id, void *user_data);
static void display_update(void);
static bool fast_timer_cb(struct repeating_timer *t);
static bool time_timer_cb(struct repeating_timer *t);
static bool dot_timer_cb(struct repeating_timer *t);
static bool brightness_timer_cb(struct repeating_timer *t);
static bool scroll_cb(struct repeating_timer *t);
static bool morph_cb(struct repeating_timer *t);
static bool boot_cb(struct repeating_timer *t);
static bool wifi_cb(struct repeating_timer *t);
static bool ntp_cb(struct repeating_timer *t);
static bool pulse_cb(struct repeating_timer *t);
static bool fade_out_cb(struct repeating_timer *t);
static bool fade_in_cb(struct repeating_timer *t);
static bool wave_cb(struct repeating_timer *t);
static bool matrix_rain_cb(struct repeating_timer *t);
static bool flicker_cb(struct repeating_timer *t);


bool display_init(const display_config_t *config) {
    if (!config || config->digit_count < DISPLAY_DIGIT_COUNT_MIN ||
        config->digit_count > DISPLAY_DIGIT_COUNT_MAX || config->fast_refresh_rate == 0 ||
        config->night_start_hour > 23 || config->night_end_hour > 23) {
        LOG_ERROR("Invalid config");
        return false;
    }
    if (config->light_sensor_adc < 26 || config->light_sensor_adc > 29 ||
        gpio_get_function(config->light_sensor_adc) != GPIO_FUNC_NULL) {
        LOG_ERROR("Invalid or already used ADC pin");
        return false;
    }

    s_state.data_pin = config->data_pin;
    s_state.clock_pin = config->clock_pin;
    s_state.latch_pin = config->latch_pin;
    s_state.light_sensor_adc = config->light_sensor_adc;
    s_state.digit_count = config->digit_count;
    s_state.fast_refresh_rate = config->fast_refresh_rate;
    s_state.night_start_hour = config->night_start_hour;
    s_state.night_end_hour = config->night_end_hour;
    s_state.on_effect_complete = config->on_effect_complete;

    gpio_init(s_state.data_pin);
    gpio_init(s_state.clock_pin);
    gpio_init(s_state.latch_pin);
    gpio_set_dir(s_state.data_pin, GPIO_OUT);
    gpio_set_dir(s_state.clock_pin, GPIO_OUT);
    gpio_set_dir(s_state.latch_pin, GPIO_OUT);

    adc_init();
    adc_gpio_init(s_state.light_sensor_adc);
    adc_select_input(s_state.light_sensor_adc - 26);

    for (int i = 0; i < DISPLAY_MAX_DIGITS; i++) {
        s_state.display_buffer[i] = SEGMENT_CODE_CLEAR;
        s_state.digit_brightness[i] = DISPLAY_BRIGHTNESS_MAX;
    }

    int lock_num = spin_lock_claim_unused(true);
    if (lock_num < 0) {
        LOG_ERROR("No spin locks available");
        return false;
    }
    s_lock = spin_lock_init(lock_num);

    if (!add_repeating_timer_us(1000000 / (s_state.fast_refresh_rate * s_state.digit_count),
                                fast_timer_cb, NULL, &s_state.fast_timer)) {
        LOG_ERROR("Failed to start fast timer");
        spin_lock_unclaim(lock_num);
        s_display_active = false;
        return false;
    }
    s_display_active = true;
    LOG_INFO("Display initialized");
    return true;
}

bool display_is_initialized(void) {
    return s_display_active;
}

bool display_start_time_updates(void) {
    if (!s_display_active) return false;
    if (s_state.time_timer_active) {
        LOG_WARN("Time updates already active");
        return false;
    }
    if (!add_repeating_timer_ms(200, time_timer_cb, NULL, &s_state.time_timer)) {
        LOG_ERROR("Failed to start time timer");
        return false;
    }
    s_state.time_timer_active = true;
    LOG_INFO("Time updates started");
    return true;
}

void display_enable_night_mode(bool enable) {
    s_state.night_mode_enabled = enable;
    update_final_brightness();
}

void display_enable_adaptive_brightness(bool enable) {
    s_state.adaptive_brightness_enabled = enable;
    update_final_brightness();
    if (enable && !s_state.brightness_timer_active) {
        if (!add_repeating_timer_ms(1000, brightness_timer_cb, NULL, &s_state.brightness_timer)) {
            LOG_ERROR("Failed to start brightness timer");
        } else {
            s_state.brightness_timer_active = true;
        }
    } else if (!enable && s_state.brightness_timer_active) {
        cancel_repeating_timer(&s_state.brightness_timer);
        s_state.brightness_timer_active = false;
    }
}

void display_enable_manual_brightness(bool enable) {
    s_state.manual_brightness_enabled = enable;
    update_final_brightness();
}

void display_set_brightness(uint8_t brightness) {
    s_state.user_brightness = (brightness > DISPLAY_BRIGHTNESS_MAX) ? DISPLAY_BRIGHTNESS_MAX : brightness;
    update_final_brightness();
}

bool display_set_digit_brightness(const uint8_t *brightness_array) {
    if (!s_display_active || !brightness_array) {
        LOG_ERROR("Brightness array is NULL or display not initialized");
        return false;
    }
    uint32_t flags = spin_lock_blocking(s_lock);
    for (int i = 0; i < s_state.digit_count; i++) {
        s_state.digit_brightness[i] = (brightness_array[i] > DISPLAY_BRIGHTNESS_MAX) ?
                                      DISPLAY_BRIGHTNESS_MAX : brightness_array[i];
    }
    spin_unlock(s_lock, flags);
    return true;
}

const uint8_t* display_get_digit_brightness(void) {
    return s_state.digit_brightness;
}

void display_set_dot_blinking(bool enable) {
    if (!s_display_active) return;
    if (enable && !s_state.dot_timer_active) {
        if (!add_repeating_timer_ms(1000, dot_timer_cb, NULL, &s_state.dot_timer)) {
            LOG_ERROR("Failed to start dot timer");
        } else {
            s_state.dot_timer_active = true;
            s_state.dot_blinking_active = true;
        }
    } else if (!enable && s_state.dot_timer_active) {
        cancel_repeating_timer(&s_state.dot_timer);
        s_state.dot_timer_active = false;
        s_state.dot_blinking_active = false;
        s_state.dot_blink_flag = false;
    }
}

void display_print(const char *text) {
    if (!s_display_active || !text) return;
    g_display_mode = DISPLAY_MODE_EFF;
    uint32_t flags = spin_lock_blocking(s_lock);
    size_t len = strlen(text);
    for (int i = 0; i < s_state.digit_count; i++) {
        if (i < (int)len && text[i] != '\0') {
            char ch = text[i];
            if (ch == '.') s_state.display_buffer[i] = SEGMENT_CODE_DOT;
            else if (ch >= '0' && ch <= '9') s_state.display_buffer[i] = ch - '0';
            else if (ch == 'E' || ch == 'e') s_state.display_buffer[i] = SEGMENT_CODE_E;
            else if (ch == '_') s_state.display_buffer[i] = SEGMENT_CODE_UNDERSCORE;
            else if (ch == '-') s_state.display_buffer[i] = SEGMENT_CODE_DASH;
            else s_state.display_buffer[i] = SEGMENT_CODE_CLEAR;
        } else {
            s_state.display_buffer[i] = SEGMENT_CODE_CLEAR;
        }
    }
    spin_unlock(s_lock, flags);
}

void display_print_digital(int number) {
    if (!s_display_active) return;
    g_display_mode = DISPLAY_MODE_EFF;
    uint32_t flags = spin_lock_blocking(s_lock);
    for (int i = 0; i < s_state.digit_count; i++) {
        s_state.display_buffer[i] = SEGMENT_CODE_CLEAR;
    }
    bool negative = (number < 0);
    if (negative) number = -number;
    int idx = s_state.digit_count - 1;
    if (number == 0) {
        s_state.display_buffer[idx] = 0;
    } else {
        while (number > 0 && idx >= 0) {
            s_state.display_buffer[idx] = (uint8_t)(number % 10);
            number /= 10;
            idx--;
        }
    }
    if (negative && idx >= 0) {
        s_state.display_buffer[idx] = SEGMENT_CODE_DASH;
    }
    spin_unlock(s_lock, flags);
}

void display_error(uint8_t code) {
    if (!s_display_active) return;
    g_display_mode = DISPLAY_MODE_EFF;
    char err_str[4] = {'E', '_', '0' + (code % 10), '\0'};
    display_print(err_str);
}

void display_scrolling_digits(const char *digits) {
    if (!s_display_active || !digits || strlen(digits) <= s_state.digit_count) return;
    g_display_mode = DISPLAY_MODE_EFF;
    if (s_state.scroll_timer_active) {
        cancel_repeating_timer(&s_state.scroll_timer);
        s_state.scroll_timer_active = false;
    }
    strncpy(s_state.scroll_str, digits, 63);
    s_state.scroll_str[63] = '\0';
    s_state.scroll_len = strlen(s_state.scroll_str);
    s_state.scroll_pos = 0;
    if (!add_repeating_timer_ms(300, scroll_cb, NULL, &s_state.scroll_timer)) {
        LOG_ERROR("Failed to start scroll timer");
        return;
    }
    s_state.scroll_timer_active = true;
}

bool display_morph_to(int target, uint32_t duration_ms, int steps) {
    if (!s_display_active || duration_ms == 0 || steps <= 0) return false;
    g_display_mode = DISPLAY_MODE_EFF;
    if (s_state.morph_timer_active) {
        cancel_repeating_timer(&s_state.morph_timer);
        s_state.morph_timer_active = false;
    }
    s_state.morph_step = 0;
    s_state.morph_steps = steps;
    for (int i = 0; i < s_state.digit_count; i++) {
        s_state.morph_target_buffer[i] = SEGMENT_CODE_CLEAR;
    }
    bool negative = (target < 0);
    if (negative) target = -target;
    int idx = s_state.digit_count - 1;
    if (target == 0) {
        s_state.morph_target_buffer[idx] = 0;
    } else {
        while (target > 0 && idx >= 0) {
            s_state.morph_target_buffer[idx] = (uint8_t)(target % 10);
            target /= 10;
            idx--;
        }
    }
    if (negative && idx >= 0) {
        s_state.morph_target_buffer[idx] = SEGMENT_CODE_DASH;
    }
    if (!add_repeating_timer_ms((int32_t)(duration_ms / s_state.morph_steps),
                                morph_cb, NULL, &s_state.morph_timer)) {
        LOG_ERROR("Failed to start morph timer");
        return false;
    }
    s_state.morph_timer_active = true;
    return true;
}

bool display_effect_boot_animation(uint32_t duration_ms) {
    if (!s_display_active || duration_ms == 0) return false;

    // Сохраняем базовый буфер для оверлея
    overlay_save_base_buffer();

    g_display_mode = DISPLAY_MODE_EFF;
    if (s_state.boot_timer_active) {
        cancel_repeating_timer(&s_state.boot_timer);
        s_state.boot_timer_active = false;
    }
    s_state.boot_step = 0;
    if (!add_repeating_timer_ms((int32_t)(duration_ms / 10),
                                boot_cb, NULL, &s_state.boot_timer)) {
        LOG_ERROR("Failed to start boot timer");
        return false;
    }
    s_state.boot_timer_active = true;
    return true;
}

bool display_effect_connecting_wifi(uint32_t duration_ms) {
    if (!s_display_active || duration_ms == 0) return false;

    // Сохраняем базовый буфер для оверлея
    overlay_save_base_buffer();

    g_display_mode = DISPLAY_MODE_EFF;
    if (s_state.wifi_timer_active) {
        cancel_repeating_timer(&s_state.wifi_timer);
        s_state.wifi_timer_active = false;
    }
    s_state.wifi_step = 0;
    if (!add_repeating_timer_ms((int32_t)(duration_ms / 10),
                                wifi_cb, NULL, &s_state.wifi_timer)) {
        LOG_ERROR("Failed to start wifi timer");
        return false;
    }
    s_state.wifi_timer_active = true;
    return true;
}

bool display_effect_ntp_sync(uint32_t duration_ms) {
    if (!s_display_active || duration_ms == 0) return false;

    // Сохраняем базовый буфер для оверлея
    overlay_save_base_buffer();

    g_display_mode = DISPLAY_MODE_EFF;
    if (s_state.ntp_timer_active) {
        cancel_repeating_timer(&s_state.ntp_timer);
        s_state.ntp_timer_active = false;
    }
    s_state.ntp_step = 0;
    s_state.ntp_loop = 0;
    if (!add_repeating_timer_ms((int32_t)(duration_ms / 18),
                                ntp_cb, NULL, &s_state.ntp_timer)) {
        LOG_ERROR("Failed to start ntp timer");
        return false;
    }
    s_state.ntp_timer_active = true;
    return true;
}

bool display_effect_pulse(uint32_t duration_ms, uint8_t cycles) {
    if (!s_display_active || duration_ms == 0 || cycles == 0) return false;
    g_display_mode = DISPLAY_MODE_EFF;

    if (s_state.pulse_timer_active) {
        cancel_repeating_timer(&s_state.pulse_timer);
        s_state.pulse_timer_active = false;
    }

    s_state.pulse_step = 0;
    s_state.pulse_cycles = (int)cycles;

    uint32_t frame_ms = 16; // ~60 FPS
    s_state.pulse_total_steps = (int)(duration_ms / frame_ms);
    if (s_state.pulse_total_steps <= 0) s_state.pulse_total_steps = 1;

    if (!add_repeating_timer_ms((int32_t)frame_ms, pulse_cb, NULL, &s_state.pulse_timer)) {
        LOG_ERROR("Failed to start pulse timer");
        return false;
    }
    s_state.pulse_timer_active = true;
    return true;
}

bool display_effect_fade_out(uint32_t duration_ms) {
    if (!s_display_active || duration_ms == 0) return false;
    g_display_mode = DISPLAY_MODE_EFF;

    if (s_state.fade_out_timer_active) {
        cancel_repeating_timer(&s_state.fade_out_timer);
        s_state.fade_out_timer_active = false;
    }

    s_state.fade_out_step = 0;
    uint32_t frame_ms = 16; // ~60 FPS
    s_state.fade_out_total_steps = (int)(duration_ms / frame_ms);
    if (s_state.fade_out_total_steps <= 0) s_state.fade_out_total_steps = 1;

    if (!add_repeating_timer_ms((int32_t)frame_ms, fade_out_cb, NULL, &s_state.fade_out_timer)) {
        LOG_ERROR("Failed to start fade out timer");
        return false;
    }
    s_state.fade_out_timer_active = true;
    return true;
}

bool display_effect_fade_in(uint32_t duration_ms) {
    if (!s_display_active || duration_ms == 0) return false;
    g_display_mode = DISPLAY_MODE_EFF;

    if (s_state.fade_in_timer_active) {
        cancel_repeating_timer(&s_state.fade_in_timer);
        s_state.fade_in_timer_active = false;
    }

    s_state.fade_in_step = 0;
    uint32_t frame_ms = 16; // ~60 FPS
    s_state.fade_in_total_steps = (int)(duration_ms / frame_ms);
    if (s_state.fade_in_total_steps <= 0) s_state.fade_in_total_steps = 1;

    uint32_t flags = spin_lock_blocking(s_lock);
    for (int i = 0; i < s_state.digit_count; i++) {
        s_state.digit_brightness[i] = 0;
    }
    spin_unlock(s_lock, flags);

    if (!add_repeating_timer_ms((int32_t)frame_ms, fade_in_cb, NULL, &s_state.fade_in_timer)) {
        LOG_ERROR("Failed to start fade in timer");
        return false;
    }
    s_state.fade_in_timer_active = true;
    return true;
}

bool display_effect_brightness_wave(uint32_t duration_ms) {
    if (!s_display_active || duration_ms == 0) return false;
    g_display_mode = DISPLAY_MODE_EFF;
    if (s_state.wave_timer_active) {
        cancel_repeating_timer(&s_state.wave_timer);
        s_state.wave_timer_active = false;
    }
    s_state.wave_step = 0;
    uint32_t frame_ms = 20; // ~50 FPS
    s_state.wave_total_steps = (int)(duration_ms / frame_ms);
    if (s_state.wave_total_steps <= 0) s_state.wave_total_steps = 1;

    if (!add_repeating_timer_ms((int32_t)frame_ms, wave_cb, NULL, &s_state.wave_timer)) {
        LOG_ERROR("Failed to start wave timer");
        return false;
    }
    s_state.wave_timer_active = true;
    return true;
}


bool display_effect_matrix_rain(uint32_t duration_ms, uint32_t frame_ms) {
    if (!s_display_active || duration_ms == 0 || frame_ms == 0) return false;

    g_display_mode = DISPLAY_MODE_EFF;

    // Останавливаем старый "дождь", если был
    if (s_matrix_timer_active) {
        cancel_repeating_timer(&s_matrix_timer);
        s_matrix_timer_active = false;
    }

    s_matrix_step = 0;
    s_matrix_total_steps = duration_ms / (int)frame_ms;
    if (s_matrix_total_steps <= 0) s_matrix_total_steps = 1;

    // минимальная "подсветка фона" для потрескивающего эффекта
    s_matrix_min_brightness = 5;

    // Сразу очищаем яркость — первые "капли" зажгутся в колбэке
    uint32_t flags = spin_lock_blocking(s_lock);
    for (int i = 0; i < s_state.digit_count; i++) {
        s_state.digit_brightness[i] = 0;
    }
    spin_unlock(s_lock, flags);

    if (!add_repeating_timer_ms((int32_t)frame_ms, matrix_rain_cb, NULL, &s_matrix_timer)) {
        LOG_ERROR("Failed to start matrix rain timer");
        return false;
    }

    s_matrix_timer_active = true;
    LOG_INFO("Matrix rain started: duration=%u ms, frame=%u ms", (unsigned)duration_ms, (unsigned)frame_ms);
    return true;
}



bool display_effect_dynamic_flicker(uint32_t min_interval_ms, uint32_t max_interval_ms, uint32_t flicker_duration_ms) {
    if (!s_display_active || min_interval_ms == 0 || max_interval_ms <= min_interval_ms || flicker_duration_ms == 0) return false;
    if (g_display_mode != DISPLAY_MODE_TIME) {
        g_display_mode = DISPLAY_MODE_TIME;
    }
    if (s_state.flicker_timer_active) {
        cancel_repeating_timer(&s_state.flicker_timer);
        s_state.flicker_timer_active = false;
    }
    s_state.flicker_min_interval_ms = min_interval_ms;
    s_state.flicker_max_interval_ms = max_interval_ms;
    s_state.flicker_duration_ms = flicker_duration_ms;
    s_state.flicker_step = 0;
    srand(time_us_32());
    if (!add_repeating_timer_ms((int32_t)min_interval_ms, flicker_cb, NULL, &s_state.flicker_timer)) {
        LOG_ERROR("Failed to start flicker timer");
        return false;
    }
    s_state.flicker_timer_active = true;
    LOG_INFO("Dynamic flicker started");
    return true;
}

bool display_flicker_triggered(void) {
    return s_flicker_triggered;
}

void display_effect_stop_flicker(void) {
    if (!s_display_active) return;
    if (s_state.flicker_timer_active) {
        cancel_repeating_timer(&s_state.flicker_timer);
        s_state.flicker_timer_active = false;
    }
    if (s_state.flicker_digit >= 0 && s_state.flicker_bit >= 0) {
        s_state.display_buffer[s_state.flicker_digit] = s_state.flicker_original_buffer[s_state.flicker_digit];
    }
    if (s_state.on_effect_complete) s_state.on_effect_complete();
}

void display_stop_all_effects(void) {
    if (!s_display_active) return;

    // Если какой-то оверлей (boot/wifi/ntp) сохранял базу — восстановим её
    overlay_restore_base_buffer();

    uint32_t flags = spin_lock_blocking(s_lock);
    if (s_state.scroll_timer_active) { cancel_repeating_timer(&s_state.scroll_timer); s_state.scroll_timer_active = false; }
    if (s_state.morph_timer_active) { cancel_repeating_timer(&s_state.morph_timer); s_state.morph_timer_active = false; }
    if (s_state.boot_timer_active) { cancel_repeating_timer(&s_state.boot_timer); s_state.boot_timer_active = false; }
    if (s_state.wifi_timer_active) { cancel_repeating_timer(&s_state.wifi_timer); s_state.wifi_timer_active = false; }
    if (s_state.ntp_timer_active) { cancel_repeating_timer(&s_state.ntp_timer); s_state.ntp_timer_active = false; }
    if (s_state.pulse_timer_active) { cancel_repeating_timer(&s_state.pulse_timer); s_state.pulse_timer_active = false; }
    if (s_state.fade_out_timer_active) { cancel_repeating_timer(&s_state.fade_out_timer); s_state.fade_out_timer_active = false; }
    if (s_state.fade_in_timer_active) { cancel_repeating_timer(&s_state.fade_in_timer); s_state.fade_in_timer_active = false; }

    if (s_state.wave_timer_active) { cancel_repeating_timer(&s_state.wave_timer); s_state.wave_timer_active = false; }

    if (s_matrix_timer_active) {cancel_repeating_timer(&s_matrix_timer);s_matrix_timer_active = false;}

    if (s_state.flicker_timer_active) {
        cancel_repeating_timer(&s_state.flicker_timer);
        s_state.flicker_timer_active = false;
        for (int i = 0; i < s_state.digit_count; i++) {
            s_state.display_buffer[i] = s_state.flicker_original_buffer[i];
        }
    }
    if (s_state.dot_timer_active) {
        cancel_repeating_timer(&s_state.dot_timer);
        s_state.dot_timer_active = false;
        s_state.dot_blinking_active = false;
        s_state.dot_blink_flag = false;
    }
    g_display_mode = DISPLAY_MODE_TIME;
    for (int i = 0; i < s_state.digit_count; i++) {
        s_state.digit_brightness[i] = DISPLAY_BRIGHTNESS_MAX;
    }
    s_state.display_brightness = s_state.user_brightness;
    if (s_state.on_effect_complete) s_state.on_effect_complete();
    LOG_INFO("All effects stopped");
    spin_unlock(s_lock, flags);
}

const volatile uint8_t* display_get_buffer(void) {
    return s_state.display_buffer;
}

static void update_final_brightness(void) {
    if (!s_display_active) return;
    uint8_t final = DISPLAY_BRIGHTNESS_MAX;
    uint32_t flags = spin_lock_blocking(s_lock);
    if (s_state.manual_brightness_enabled) {
        final = s_state.user_brightness;
    } else if (s_state.adaptive_brightness_enabled) {
        adc_select_input(s_state.light_sensor_adc - 26);
        uint16_t raw = adc_read();
        final = (uint8_t)((raw * DISPLAY_BRIGHTNESS_MAX) / 4095);
        if (final < 5) final = 5;
    } else if (s_state.night_mode_enabled) {
        if (rtc_running()) {
            datetime_t now;
            rtc_get_datetime(&now);
            bool is_night = (s_state.night_start_hour <= s_state.night_end_hour) ?
                            (now.hour >= s_state.night_start_hour && now.hour < s_state.night_end_hour) :
                            (now.hour >= s_state.night_start_hour || now.hour < s_state.night_end_hour);
            if (is_night) {
                final = 10;
            } else {
                final = s_state.user_brightness;
            }
        }
    }
    if (s_state.display_brightness != final) {
        s_state.display_brightness = final;
        for (int i = 0; i < s_state.digit_count; i++) {
            s_state.digit_brightness[i] = final;
        }
        LOG_INFO("Brightness updated to %u", final);
    }
    spin_unlock(s_lock, flags);
}

static void shift_out(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        gpio_put(s_state.data_pin, (data >> (7 - i)) & 1);
        gpio_put(s_state.clock_pin, 1);
        gpio_put(s_state.clock_pin, 0);
    }
}

static void latch(void) {
    gpio_put(s_state.latch_pin, 1);
    gpio_put(s_state.latch_pin, 0);
}

static int64_t display_clear_cb(alarm_id_t id, void *user_data) {
    if (!s_display_active) return 0;
    shift_out(0x00);
    shift_out(0x00);
    latch();
    s_state.display_clear_alarm = 0;
    return 0;
}

static void display_update(void) {
    if (!s_display_active) return;
    uint32_t flags = spin_lock_blocking(s_lock);
    shift_out(1 << s_state.current_digit);
    uint8_t code = 0;
    uint8_t idx = s_state.display_buffer[s_state.current_digit];
    if (idx < ARRAY_SIZE(SEGMENT_CODES)) {
        code = SEGMENT_CODES[idx];
    }
    if (s_state.dot_blink_flag && s_state.current_digit == 1) {
        code |= 0b10000000;
    }
    shift_out(code);
    latch();
    s_state.current_digit = (s_state.current_digit + 1) % s_state.digit_count;
    if (s_state.display_clear_alarm) {
        cancel_alarm(s_state.display_clear_alarm);
    }
    uint32_t brightness = (g_display_mode == DISPLAY_MODE_EFF) ?
                          s_state.digit_brightness[s_state.current_digit] : s_state.display_brightness;
    uint32_t delay_us = (brightness * 2000u) / 100u;
    s_state.display_clear_alarm = add_alarm_in_us((int64_t)delay_us, display_clear_cb, NULL, true);
    if (s_state.display_clear_alarm < 0) {
        LOG_ERROR("Failed to set clear alarm");
    }
    spin_unlock(s_lock, flags);
}

static bool fast_timer_cb(struct repeating_timer *t) {
    display_update();
    return true;
}

static bool time_timer_cb(struct repeating_timer *t) {
    if (!s_display_active) return false;
    if (g_display_mode == DISPLAY_MODE_TIME && s_state.flicker_step == 0) {
        if (rtc_running()) {
            datetime_t now;
            rtc_get_datetime(&now);
            update_final_brightness();
            uint32_t flags = spin_lock_blocking(s_lock);
            s_state.display_buffer[0] = (now.hour / 10) % 10;
            s_state.display_buffer[1] = now.hour % 10;
            s_state.display_buffer[2] = (now.min / 10) % 10;
            s_state.display_buffer[3] = now.min % 10;
            spin_unlock(s_lock, flags);
        }
    }
    return true;
}

static bool dot_timer_cb(struct repeating_timer *t) {
    s_state.dot_blink_flag = !s_state.dot_blink_flag;
    return true;
}

static bool brightness_timer_cb(struct repeating_timer *t) {
    update_final_brightness();
    return true;
}

static bool scroll_cb(struct repeating_timer *t) {
    if (s_state.scroll_pos >= s_state.scroll_len + s_state.digit_count) {
        g_display_mode = DISPLAY_MODE_TIME;
        s_state.scroll_timer_active = false;
        if (s_state.on_effect_complete) s_state.on_effect_complete();
        return false;
    }
    uint32_t flags = spin_lock_blocking(s_lock);
    for (int d = 0; d < s_state.digit_count; d++) {
        int idx = s_state.scroll_pos + d - (s_state.digit_count - 1);
        if (idx < 0 || idx >= s_state.scroll_len) {
            s_state.display_buffer[d] = SEGMENT_CODE_CLEAR;
        } else {
            char ch = s_state.scroll_str[idx];
            if (ch == '.') s_state.display_buffer[d] = SEGMENT_CODE_DOT;
            else if (ch >= '0' && ch <= '9') s_state.display_buffer[d] = ch - '0';
            else if (ch == 'E' || ch == 'e') s_state.display_buffer[d] = SEGMENT_CODE_E;
            else if (ch == '_') s_state.display_buffer[d] = SEGMENT_CODE_UNDERSCORE;
            else if (ch == '-') s_state.display_buffer[d] = SEGMENT_CODE_DASH;
            else s_state.display_buffer[d] = SEGMENT_CODE_CLEAR;
        }
    }
    s_state.scroll_pos++;
    spin_unlock(s_lock, flags);
    return true;
}

static bool morph_cb(struct repeating_timer *t) {
    if (s_state.morph_step >= s_state.morph_steps) {
        uint32_t flags = spin_lock_blocking(s_lock);
        for (int i = 0; i < s_state.digit_count; i++) {
            s_state.display_buffer[i] = s_state.morph_target_buffer[i];
        }
        spin_unlock(s_lock, flags);
        g_display_mode = DISPLAY_MODE_TIME;
        s_state.morph_timer_active = false;
        if (s_state.on_effect_complete) s_state.on_effect_complete();
        return false;
    }
    uint32_t flags = spin_lock_blocking(s_lock);
    for (int i = 0; i < s_state.digit_count; i++) {
        uint8_t curr_idx = s_state.display_buffer[i];
        uint8_t target_idx = s_state.morph_target_buffer[i];
        if (curr_idx == target_idx) continue;

        uint8_t curr = (curr_idx < ARRAY_SIZE(SEGMENT_CODES)) ? SEGMENT_CODES[curr_idx] : 0;
        uint8_t target = (target_idx < ARRAY_SIZE(SEGMENT_CODES)) ? SEGMENT_CODES[target_idx] : 0;
        uint8_t new_code = 0;

        uint32_t progress = (s_state.morph_step * 100) / s_state.morph_steps;
        for (int bit = 0; bit < 8; bit++) {
            int curr_bit = (curr >> bit) & 1;
            int target_bit = (target >> bit) & 1;
            if (curr_bit == target_bit) {
                new_code |= curr_bit << bit;
            } else if (curr_bit == 0 && target_bit == 1) {
                if (progress >= (uint32_t)((bit + 1) * 12)) new_code |= 1 << bit;
            } else if (curr_bit == 1 && target_bit == 0) {
                if (progress < (uint32_t)((bit + 1) * 12)) new_code |= 1 << bit;
            }
        }

        uint8_t best_idx = curr_idx;
        int min_diff = 255;
        for (int j = 0; j < ARRAY_SIZE(SEGMENT_CODES); j++) {
            int diff = 0;
            for (int bit = 0; bit < 8; bit++) {
                if (((SEGMENT_CODES[j] >> bit) & 1) != ((new_code >> bit) & 1)) diff++;
            }
            if (diff < min_diff) {
                min_diff = diff;
                best_idx = j;
            }
        }
        s_state.display_buffer[i] = best_idx;
    }
    s_state.morph_step++;
    spin_unlock(s_lock, flags);
    return true;
}

static bool boot_cb(struct repeating_timer *t) {
    if (s_state.boot_step >= 10) {
        // Эффект завершился — вернём базовый буфер
        overlay_restore_base_buffer();

        g_display_mode = DISPLAY_MODE_TIME;
        s_state.boot_timer_active = false;
        if (s_state.on_effect_complete) s_state.on_effect_complete();
        return false;
    }
    uint32_t flags = spin_lock_blocking(s_lock);
    for (int d = 0; d < s_state.digit_count; d++) {
        s_state.display_buffer[d] = (uint8_t)(s_state.boot_step % 10);
    }
    s_state.boot_step++;
    spin_unlock(s_lock, flags);
    return true;
}

static bool wifi_cb(struct repeating_timer *t) {
    if (s_state.wifi_step >= 10) {
        // Эффект завершился — вернём базовый буфер
        overlay_restore_base_buffer();

        g_display_mode = DISPLAY_MODE_TIME;
        s_state.wifi_timer_active = false;
        if (s_state.on_effect_complete) s_state.on_effect_complete();
        return false;
    }
    uint32_t flags = spin_lock_blocking(s_lock);
    for (int d = 0; d < s_state.digit_count; d++) {
        s_state.display_buffer[d] = (s_state.wifi_step % 2 == 0) ? 8 : SEGMENT_CODE_CLEAR;
    }
    s_state.wifi_step++;
    spin_unlock(s_lock, flags);
    return true;
}

static bool ntp_cb(struct repeating_timer *t) {
    static const int pattern[] = {0, 1, 2, 3, 2, 1};
    if (s_state.ntp_loop >= 3) {
        // Эффект завершился — вернём базовый буфер
        overlay_restore_base_buffer();

        g_display_mode = DISPLAY_MODE_TIME;
        s_state.ntp_timer_active = false;
        if (s_state.on_effect_complete) s_state.on_effect_complete();
        return false;
    }
    uint32_t flags = spin_lock_blocking(s_lock);
    for (int d = 0; d < s_state.digit_count; d++) {
        s_state.display_buffer[d] = SEGMENT_CODE_CLEAR;
    }
    s_state.display_buffer[pattern[s_state.ntp_step]] = 8;
    s_state.ntp_step++;
    if (s_state.ntp_step >= 6) {
        s_state.ntp_step = 0;
        s_state.ntp_loop++;
    }
    spin_unlock(s_lock, flags);
    return true;
}

static bool pulse_cb(struct repeating_timer *t) {
    if (s_state.pulse_step >= s_state.pulse_total_steps) {
        uint32_t flags = spin_lock_blocking(s_lock);
        for (int i = 0; i < s_state.digit_count; i++) {
            s_state.digit_brightness[i] = DISPLAY_BRIGHTNESS_MAX;
        }
        spin_unlock(s_lock, flags);

        g_display_mode = DISPLAY_MODE_TIME;
        s_state.pulse_timer_active = false;
        if (s_state.on_effect_complete) s_state.on_effect_complete();
        return false;
    }

    float progress = (float)s_state.pulse_step / (float)s_state.pulse_total_steps;
    float phi = progress * 2.0f * PI_F * (float)s_state.pulse_cycles;

    float val = -cosf(phi);                  // старт из минимума
    float normalized = (val + 1.0f) * 0.5f;  // 0..1
    normalized = gamma_correct(normalized);

    uint8_t brightness = 8 + (uint8_t)(normalized * (DISPLAY_BRIGHTNESS_MAX - 8));

    uint32_t flags = spin_lock_blocking(s_lock);
    for (int i = 0; i < s_state.digit_count; i++) {
        s_state.digit_brightness[i] = brightness;
    }
    spin_unlock(s_lock, flags);

    s_state.pulse_step++;
    return true;
}

static bool fade_out_cb(struct repeating_timer *t) {
    if (s_state.fade_out_step >= s_state.fade_out_total_steps) {
        uint32_t flags = spin_lock_blocking(s_lock);
        for (int i = 0; i < s_state.digit_count; i++) {
            s_state.digit_brightness[i] = 0;
        }
        spin_unlock(s_lock, flags);

        g_display_mode = DISPLAY_MODE_TIME;
        s_state.fade_out_timer_active = false;
        if (s_state.on_effect_complete) s_state.on_effect_complete();
        return false;
    }

    float progress = (float)s_state.fade_out_step / (float)s_state.fade_out_total_steps;
    float n = 1.0f - progress;   // 1 → 0
    n = gamma_correct(n);

    uint8_t brightness = (uint8_t)(n * DISPLAY_BRIGHTNESS_MAX);

    uint32_t flags = spin_lock_blocking(s_lock);
    for (int i = 0; i < s_state.digit_count; i++) {
        s_state.digit_brightness[i] = brightness;
    }
    spin_unlock(s_lock, flags);

    s_state.fade_out_step++;
    return true;
}

static bool fade_in_cb(struct repeating_timer *t) {
    if (s_state.fade_in_step >= s_state.fade_in_total_steps) {
        uint32_t flags = spin_lock_blocking(s_lock);
        for (int i = 0; i < s_state.digit_count; i++) {
            s_state.digit_brightness[i] = DISPLAY_BRIGHTNESS_MAX;
        }
        spin_unlock(s_lock, flags);

        g_display_mode = DISPLAY_MODE_TIME;
        s_state.fade_in_timer_active = false;
        if (s_state.on_effect_complete) s_state.on_effect_complete();
        return false;
    }

    float progress = (float)s_state.fade_in_step / (float)s_state.fade_in_total_steps;
    float n = gamma_correct(progress);

    uint8_t brightness = (uint8_t)(n * DISPLAY_BRIGHTNESS_MAX);

    uint32_t flags = spin_lock_blocking(s_lock);
    for (int i = 0; i < s_state.digit_count; i++) {
        s_state.digit_brightness[i] = brightness;
    }
    spin_unlock(s_lock, flags);

    s_state.fade_in_step++;
    return true;
}

static bool wave_cb(struct repeating_timer *t) {
    if (s_state.wave_step >= s_state.wave_total_steps) {
        uint32_t flags = spin_lock_blocking(s_lock);
        for (int i = 0; i < s_state.digit_count; i++) {
            s_state.digit_brightness[i] = DISPLAY_BRIGHTNESS_MAX;
        }
        spin_unlock(s_lock, flags);

        g_display_mode = DISPLAY_MODE_TIME;
        s_state.wave_timer_active = false;
        if (s_state.on_effect_complete) s_state.on_effect_complete();
        return false;
    }

    float time_phase = (float)s_state.wave_step * 0.18f;

    uint32_t flags = spin_lock_blocking(s_lock);
    for (int i = 0; i < s_state.digit_count; i++) {
        float val = sinf(time_phase - (i * 0.7f));
        float normalized = (val + 1.0f) * 0.5f;
        normalized = gamma_correct(normalized);

        int brightness = 5 + (int)(normalized * (DISPLAY_BRIGHTNESS_MAX - 5));
        s_state.digit_brightness[i] = (uint8_t)brightness;
    }
    spin_unlock(s_lock, flags);

    s_state.wave_step++;
    return true;
}



static bool matrix_rain_cb(struct repeating_timer *t) {
    if (!s_display_active) return false;

    // Если эффект отработал своё —
    if (s_matrix_step >= s_matrix_total_steps) {
        uint32_t flags = spin_lock_blocking(s_lock);
        // Вернём нормальную равномерную яркость
        for (int i = 0; i < s_state.digit_count; i++) {
            s_state.digit_brightness[i] = DISPLAY_BRIGHTNESS_MAX;
        }
        spin_unlock(s_lock, flags);

        s_matrix_timer_active = false;
        g_display_mode = DISPLAY_MODE_TIME;
        if (s_state.on_effect_complete) s_state.on_effect_complete();
        LOG_INFO("Matrix rain finished");
        return false;
    }

    uint32_t flags = spin_lock_blocking(s_lock);

    // Плавно затемняем все цифры
    for (int i = 0; i < s_state.digit_count; i++) {
        uint8_t b = s_state.digit_brightness[i];

        if (b > s_matrix_min_brightness) {
            uint8_t dec = 5;  // скорость затухания "шлейфа"
            if (b <= s_matrix_min_brightness + dec)
                b = s_matrix_min_brightness;
            else
                b = (uint8_t)(b - dec);
        }

        s_state.digit_brightness[i] = b;
    }

    // Зажигаем одну случайную цифру на максимум — новая "капля"
    int random_digit = rand() % s_state.digit_count;
    s_state.digit_brightness[random_digit] = DISPLAY_BRIGHTNESS_MAX;

    spin_unlock(s_lock, flags);

    s_matrix_step++;
    return true;
}



static bool flicker_cb(struct repeating_timer *t) {
    if (!s_display_active || g_display_mode != DISPLAY_MODE_TIME) return false;
    if (s_state.flicker_step > 0) {
        static const int pattern[] = {1, 0, 1, 0, 1, 1, 0};
        int pattern_len = 7;
        int pattern_step = s_state.flicker_step % pattern_len;
        uint32_t flags = spin_lock_blocking(s_lock);
        if (s_state.flicker_digit >= 0 && s_state.flicker_bit >= 0) {
            uint8_t curr = s_state.display_buffer[s_state.flicker_digit];
            if (pattern[pattern_step]) {
                s_state.display_buffer[s_state.flicker_digit] = curr | (1 << s_state.flicker_bit);
            } else {
                s_state.display_buffer[s_state.flicker_digit] = curr & ~(1 << s_state.flicker_bit);
            }
        }
        s_flicker_triggered = true;
        s_state.flicker_step++;
        if (s_state.flicker_step >= (int)(s_state.flicker_duration_ms / 50)) {
            if (s_state.flicker_digit >= 0 && s_state.flicker_bit >= 0) {
                s_state.display_buffer[s_state.flicker_digit] = s_state.flicker_original_buffer[s_state.flicker_digit];
            }
            s_state.flicker_count--;
            if (s_state.flicker_count <= 0) {
                s_state.flicker_step = 0;
                s_state.flicker_digit = -1;
                s_state.flicker_bit = -1;
                t->delay_us = (rand() % (s_state.flicker_max_interval_ms - s_state.flicker_min_interval_ms) +
                               s_state.flicker_min_interval_ms) * 1000;
                LOG_INFO("Dynamic flicker cycle ended");
            }
        }
        spin_unlock(s_lock, flags);
        return true;
    } else {
        uint32_t flags = spin_lock_blocking(s_lock);
        for (int i = 0; i < s_state.digit_count; i++) {
            s_state.flicker_original_buffer[i] = s_state.display_buffer[i];
        }
        s_state.flicker_digit = rand() % s_state.digit_count;
        s_state.flicker_bit = rand() % 7;
        s_state.flicker_count = (rand() % 3) + 1;
        s_state.flicker_step = 1;
        t->delay_us = 50 * 1000;
        spin_unlock(s_lock, flags);
        return true;
    }
}
