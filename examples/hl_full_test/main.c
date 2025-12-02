#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/rtc.h"

// Подключение библиотеки
#include "display_api.h"
#include "display_font.h" 
#include "display_ll.h"

/* ==========================================================================
   НАСТРОЙКИ
   ========================================================================== */
#define DEMO_DIGIT_COUNT 4

// Пины (Raspberry Pi Pico default)
#define PIN_DATA  15
#define PIN_CLOCK 14
#define PIN_LATCH 13

// Паузы (мс)
#define DELAY_READ       2000 // Время, чтобы прочитать статичный текст
#define DELAY_TRANSITION 1000 // Пауза между эффектами

// Маска точки (индекс 1 = 2-й разряд слева: XX.XX)
#define DOT_MASK_CLOCK (1 << 1)

/* ==========================================================================
   ЭМУЛЯТОР ЧАСОВ
   ========================================================================== */
static uint8_t g_hour = 12;
static uint8_t g_min  = 0;
static uint8_t g_sec  = 0;
static absolute_time_t g_last_sec_time;

void update_clock_simulation(void) {
    absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(g_last_sec_time, now) >= 1000000) {
        g_last_sec_time = now;
        g_sec++;
        if (g_sec >= 60) {
            g_sec = 0;
            g_min++;
            if (g_min >= 60) {
                g_min = 0;
                g_hour++;
                if (g_hour >= 24) g_hour = 0;
            }
        }
    }
}

/* ==========================================================================
   СЦЕНАРИЙ
   ========================================================================== */

typedef enum {
    // --- System Start ---
    ST_BOOT,            // Overlay Boot (Self test)
    
    // --- Basic Content ---
    ST_CLOCK,           // Часы (Blinking Dot)
    ST_FADE_OUT,        // Гашение
    ST_FADE_IN,         // Включение
    ST_STATIC_NUM,      // Число (Static Dot)
    
    // --- Text FX ---
    ST_SLIDE_IN,        // Выезд текста
    ST_WAIT_READ_1,     // Пауза для чтения
    ST_MARQUEE,         // Бегущая строка
    
    // --- System Overlays ---
    ST_OV_WIFI,         // WiFi Connect
    ST_OV_NTP,          // Time Sync
    
    // --- Transparent FX (поверх часов) ---
    ST_FX_PULSE,        // Дыхание
    ST_FX_WAVE,         // Волна [NEW]
    ST_FX_SCANNER,      // Matrix/KITT
    
    // --- Destructive FX (меняют контент) ---
    ST_FX_GLITCH,       // Шум
    ST_FX_MORPH,        // Превращение
    ST_WAIT_READ_2,     // Пауза для чтения результата Morph
    ST_FX_DISSOLVE,     // Рассыпание
    
    ST_LOOP             // Рестарт
} state_t;

static state_t g_state = ST_BOOT;
static absolute_time_t g_state_start;
static bool g_state_init = false;

void next_state(state_t s) {
    g_state = s;
    g_state_start = get_absolute_time();
    g_state_init = false;
    printf("State: %d\n", s);
}

void process_demo(void) {
    absolute_time_t now = get_absolute_time();
    uint32_t elapsed = (uint32_t)(absolute_time_diff_us(g_state_start, now) / 1000);

    switch (g_state) {
        
        // 1. BOOT (2 сек)
        case ST_BOOT:
            if (!g_state_init) {
                display_overlay_boot(2000);
                g_state_init = true;
            }
            if (!display_is_overlay_running()) next_state(ST_CLOCK);
            break;

        // 2. CLOCK (3 сек)
        case ST_CLOCK:
            if (!g_state_init) {
                display_set_dots_config(DOT_MASK_CLOCK, true); // Вкл мигание
                display_set_brightness(255);
                g_state_init = true;
            }
            display_show_time(g_hour, g_min, false);
            if (elapsed > 3000) next_state(ST_FADE_OUT);
            break;

        // 3. FADE OUT (1 сек)
        case ST_FADE_OUT:
            if (!g_state_init) {
                display_fx_fade_out(1000);
                g_state_init = true;
            }
            // Пока темно, меняем контент на статический
            if (elapsed > 900) { 
                display_show_number(1234);
                display_set_dots_config(DOT_MASK_CLOCK, false); // Статическая точка
            }
            if (elapsed > 1200) next_state(ST_FADE_IN);
            break;

        // 4. FADE IN (1 сек)
        case ST_FADE_IN:
            if (!g_state_init) {
                display_fx_fade_in(1000);
                g_state_init = true;
            }
            if (!display_is_effect_running()) next_state(ST_STATIC_NUM);
            break;

        // 5. STATIC VALUE (2 сек)
        case ST_STATIC_NUM:
            // Просто показываем число "12.34" (точка статична)
            if (elapsed > 2000) next_state(ST_SLIDE_IN);
            break;

        // 6. SLIDE IN "TEST"
        case ST_SLIDE_IN:
            if (!g_state_init) {
                display_set_dots_config(0, false); // Убрать точки
                display_fx_slide_in("TEST", 150);
                g_state_init = true;
            }
            if (!display_is_effect_running()) next_state(ST_WAIT_READ_1);
            break;

        case ST_WAIT_READ_1:
            if (elapsed > DELAY_READ) next_state(ST_MARQUEE);
            break;

        // 7. MARQUEE (IP)
        case ST_MARQUEE:
            if (!g_state_init) {
                display_fx_marquee("IP: 192.168.1.1", 120);
                g_state_init = true;
            }
            if (!display_is_effect_running()) next_state(ST_OV_WIFI);
            break;

        // 8. OVERLAY WIFI
        case ST_OV_WIFI:
            if (!g_state_init) {
                display_overlay_wifi(2000);
                g_state_init = true;
            }
            if (!display_is_overlay_running()) {
                sleep_ms(DELAY_TRANSITION); // Короткая пауза
                next_state(ST_OV_NTP);
            }
            break;

        // 9. OVERLAY NTP
        case ST_OV_NTP:
            if (!g_state_init) {
                display_overlay_ntp(2500);
                g_state_init = true;
            }
            if (!display_is_overlay_running()) next_state(ST_FX_PULSE);
            break;

        // === ДАЛЕЕ ПРОЗРАЧНЫЕ ЭФФЕКТЫ (Часы идут фоном) ===

        // 10. PULSE (3 сек)
        case ST_FX_PULSE:
            if (!g_state_init) {
                display_set_dots_config(DOT_MASK_CLOCK, true); // Вернуть часы
                display_fx_pulse(3000);
                g_state_init = true;
            }
            display_show_time(g_hour, g_min, false);
            
            // Ждем окончания эффекта + пауза
            if (elapsed > 3000 + DELAY_TRANSITION) next_state(ST_FX_WAVE);
            break;

        // 11. WAVE (3 сек) - [ДОБАВЛЕНО]
        case ST_FX_WAVE:
            if (!g_state_init) {
                display_fx_wave(3000);
                g_state_init = true;
            }
            display_show_time(g_hour, g_min, false);
            
            if (elapsed > 3000 + DELAY_TRANSITION) next_state(ST_FX_SCANNER);
            break;

        // 12. SCANNER / MATRIX (3 сек)
        case ST_FX_SCANNER:
            if (!g_state_init) {
                // Период 600мс (довольно быстро бегает)
                display_fx_matrix(3000, 600);
                g_state_init = true;
            }
            display_show_time(g_hour, g_min, false);
            
            if (elapsed > 3000 + DELAY_TRANSITION) next_state(ST_FX_GLITCH);
            break;

        // === ДАЛЕЕ БЛОКИРУЮЩИЕ ЭФФЕКТЫ ===

        // 13. GLITCH
        case ST_FX_GLITCH:
            if (!g_state_init) {
                display_fx_glitch(2000);
                g_state_init = true;
            }
            // Во время глитча update не нужен, но не вредит
            display_show_time(g_hour, g_min, false);

            if (elapsed > 2000 + DELAY_TRANSITION) next_state(ST_FX_MORPH);
            break;

        // 14. MORPH ("CODE" -> "GOOD")
        case ST_FX_MORPH:
            if (!g_state_init) {
                display_set_dots_config(0, false);
                display_show_text("CODE"); // Старт с этого слова
                
                // Цель: "GOOD"
                vfd_segment_map_t target[4];
                target[0] = display_font_get_char('G');
                target[1] = display_font_get_char('O');
                target[2] = display_font_get_char('O');
                target[3] = display_font_get_char('D');

                // 2 секунды на превращение
                display_fx_morph(2000, target, 40);
                g_state_init = true;
            }
            // Ждем завершения эффекта
            if (elapsed > 2000) next_state(ST_WAIT_READ_2);
            break;

        // 15. Пауза, чтобы прочитать "GOOD"
        case ST_WAIT_READ_2:
            if (elapsed > DELAY_READ) next_state(ST_FX_DISSOLVE);
            break;

        // 16. DISSOLVE
        case ST_FX_DISSOLVE:
            if (!g_state_init) {
                display_fx_dissolve(1500);
                g_state_init = true;
            }
            // Ждем рассыпания + пауза на черном экране
            if (elapsed > 1500 + DELAY_TRANSITION) next_state(ST_LOOP);
            break;

        case ST_LOOP:
            next_state(ST_CLOCK);
            break;
    }
}

int main() {
    stdio_init_all();
    sleep_ms(2000); // Даем время USB UART подняться
    printf("=== VFD Full Showcase v1.3.7 ===\n");

    // 1. Инициализация (Custom Config)
    display_ll_config_t cfg = {
        .data_pin = PIN_DATA,
        .clock_pin = PIN_CLOCK,
        .latch_pin = PIN_LATCH,
        .digit_count = DEMO_DIGIT_COUNT,
        .refresh_rate_hz = 120
    };
    display_init_ex(&cfg);
    
    // 2. Включаем дисплей
    display_set_brightness(255);

    /* 
       ДАТЧИК ОСВЕЩЕННОСТИ (Hardware Pending)
       // display_set_auto_brightness(true);
    */

    g_last_sec_time = get_absolute_time();

    while (true) {
        // ЯДРО (Обязательно)
        display_process();
        
        // Логика времени
        update_clock_simulation();
        
        // Сценарий
        process_demo();
        
        sleep_ms(1);
    }
    return 0;
}