#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/rtc.h"
#include "display_api.h"
#include "display_font.h"

#define DEMO_DIGIT_COUNT 4

/* ==========================================================================
   ЭМУЛЯЦИЯ ЧАСОВ
   ========================================================================== */
static uint8_t g_hour = 19;
static uint8_t g_min  = 45;
static uint8_t g_sec  = 0;
static absolute_time_t g_last_sec_time;

void update_clock_simulation(void) {
    absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(g_last_sec_time, now) >= 1000000) { // 1 сек
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
   МАШИНА СОСТОЯНИЙ ДЕМО
   ========================================================================== */

typedef enum {
    STATE_BOOT_OVERLAY = 0, // Анимация загрузки
    STATE_CLOCK_NORMAL,     // Обычные часы
    STATE_FX_SLIDE_IN,      // Текст выезжает
    STATE_FX_MARQUEE,       // Бегущая строка
    STATE_OVERLAY_WIFI,     // [NEW] Симуляция подключения WiFi
    STATE_FX_PULSE,         // Дыхание
    STATE_FX_WAVE,          // Волна
    STATE_FX_SCANNER,       // KITT сканер
    STATE_FX_GLITCH,        // Глитч эффект
    STATE_FX_MORPH_TEXT,    // Превращение текста
    STATE_FX_DISSOLVE,      // Рассыпание
    STATE_FX_FADE_CYCLE,    // Fade Out -> Fade In
    STATE_RESET_LOOP        // Возврат в начало
} demo_state_t;

static demo_state_t g_state = STATE_BOOT_OVERLAY;
static absolute_time_t g_state_start_time;
static bool g_state_setup_done = false;

// Хелпер для переключения
void set_state(demo_state_t new_state) {
    g_state = new_state;
    g_state_start_time = get_absolute_time();
    g_state_setup_done = false;
    printf("State -> %d\n", new_state);
}

void process_demo_sequence(void) {
    absolute_time_t now = get_absolute_time();
    uint32_t elapsed_ms = (uint32_t)(absolute_time_diff_us(g_state_start_time, now) / 1000);

    switch (g_state) {
        
        // 1. Start Up Overlay
        case STATE_BOOT_OVERLAY:
            if (!g_state_setup_done) {
                // Запускаем Boot анимацию (перебор цифр)
                display_overlay_boot(0); 
                g_state_setup_done = true;
            }
            // Ждем пока оверлей закончится
            if (!display_is_overlay_running()) {
                set_state(STATE_CLOCK_NORMAL);
            }
            break;

        // 2. Часы 
        case STATE_CLOCK_NORMAL:
            if (!g_state_setup_done) {
                display_set_dot_blinking(true);
                display_set_brightness(255);
                g_state_setup_done = true;
            }
            display_show_time(g_hour, g_min, true);

            if (elapsed_ms > 3000) set_state(STATE_FX_SLIDE_IN);
            break;

        // 3. Slide In 
        case STATE_FX_SLIDE_IN:
            if (!g_state_setup_done) {
                display_set_dot_blinking(false);
                display_fx_slide_in("01.01", 150);
                g_state_setup_done = true;
            }
            // Ждем чуть дольше чем длится эффект, чтобы прочитать
            if (elapsed_ms > 2000 && !display_is_effect_running()) 
                set_state(STATE_FX_MARQUEE);
            break;

        // 4. Marquee (IP Адрес)
        case STATE_FX_MARQUEE:
            if (!g_state_setup_done) {
                display_fx_marquee("192.168.1.105", 150);
                g_state_setup_done = true;
            }
            if (!display_is_effect_running()) set_state(STATE_OVERLAY_WIFI);
            break;

        // 5. Overlay Interrupt 
        case STATE_OVERLAY_WIFI:
            if (!g_state_setup_done) {
                // Это оверлей "8888" мигающий, типа подключение
                display_overlay_wifi(0);
                g_state_setup_done = true;
            }
            if (!display_is_overlay_running()) set_state(STATE_FX_PULSE);
            break;

        // 6. Pulse 
        case STATE_FX_PULSE:
            if (!g_state_setup_done) {
                display_set_dot_blinking(true);
                display_fx_pulse(3000); // 3 сек дыхания
                g_state_setup_done = true;
            }
            display_show_time(g_hour, g_min, true);
            
            if (elapsed_ms > 3500) set_state(STATE_FX_WAVE);
            break;

        // 7. Wave 
        case STATE_FX_WAVE:
            if (!g_state_setup_done) {
                display_fx_wave(3000);
                g_state_setup_done = true;
            }
            display_show_time(g_hour, g_min, true);

            if (elapsed_ms > 3500) set_state(STATE_FX_SCANNER);
            break;

        // 8. Scanner 
        case STATE_FX_SCANNER:
            if (!g_state_setup_done) {
                display_fx_matrix(3000, 0);
                g_state_setup_done = true;
            }
            display_show_time(g_hour, g_min, true);

            if (elapsed_ms > 3500) set_state(STATE_FX_GLITCH);
            break;

        // 9. Glitch 
        case STATE_FX_GLITCH:
            if (!g_state_setup_done) {
                display_fx_glitch(2500);
                g_state_setup_done = true;
            }
            display_show_time(g_hour, g_min, true);

            if (elapsed_ms > 3000) set_state(STATE_FX_MORPH_TEXT);
            break;

        // 10. Morphing 
        case STATE_FX_MORPH_TEXT:
            if (!g_state_setup_done) {
                display_set_dot_blinking(false);
                display_show_text("GOOD"); // Исходный текст
                
                // Подготовка целевого буфера (слово "BYTE")
                // Нам нужно "собрать" его вручную, чтобы передать в функцию morph
                vfd_seg_t target[4];
                target[0] = display_font_get_char('B');
                target[1] = display_font_get_char('Y');
                target[2] = display_font_get_char('T');
                target[3] = display_font_get_char('E');

                display_fx_morph(2000, target, 25);
                g_state_setup_done = true;
            }
            if (elapsed_ms > 2500) set_state(STATE_FX_DISSOLVE);
            break;

        // 11. Dissolve 
        case STATE_FX_DISSOLVE:
            if (!g_state_setup_done) {
                // В буфере сейчас "BYTE" после морфинга
                display_fx_dissolve(1500);
                g_state_setup_done = true;
            }
            if (elapsed_ms > 2000) set_state(STATE_FX_FADE_CYCLE);
            break;

        // 12. Fade Cycle 
        case STATE_FX_FADE_CYCLE:
            if (!g_state_setup_done) {
                // Сначала выключаем всё (хотя после dissolve там и так почти пусто)
                display_fx_fade_out(1000);
                g_state_setup_done = true;
            }
            
            // В середине фейда ставим время обратно
            if (elapsed_ms > 1100 && elapsed_ms < 1200) {
                 display_show_time(g_hour, g_min, true);
            }

            // И плавно включаем
            if (elapsed_ms > 1200 && !display_is_effect_running()) {
                 display_fx_fade_in(1000);
                 set_state(STATE_RESET_LOOP); // Уходим на круг
            }
            break;

        case STATE_RESET_LOOP:
             if (!display_is_effect_running()) {
                 set_state(STATE_CLOCK_NORMAL);
             }
             break;
    }
}

int main() {
    stdio_init_all();
    sleep_ms(2000);
    printf("=== VFD Full HL Showcase ===\n");

    display_init(DEMO_DIGIT_COUNT);
    
    // Включаем на полную яркость для демо
    display_set_brightness(255);
    display_set_auto_brightness(false);

    g_last_sec_time = get_absolute_time();

    while (true) {
        // 1. Движок ядра дисплея
        display_process();
        
        // 2. Логика времени
        update_clock_simulation();
        
        // 3. Сценарий демонстрации
        process_demo_sequence();
        
        sleep_ms(2);
    }
    return 0;
}