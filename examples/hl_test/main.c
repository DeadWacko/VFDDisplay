#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/rtc.h"
#include "display_api.h"

/* ==========================================================================
   НАСТРОЙКИ
   ========================================================================== */
#define DEMO_DIGIT_COUNT 4

/* ==========================================================================
   ЭМУЛЯЦИЯ ЧАСОВ
   ========================================================================== */
static uint8_t g_hour = 12;
static uint8_t g_min  = 34;
static uint8_t g_sec  = 50;
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
   СЦЕНАРИЙ ДЕМОНСТРАЦИИ
   ========================================================================== */
typedef enum {
    STATE_CLOCK_NORMAL = 0, // Просто часы
    STATE_FX_SLIDE_IN,      // [NEW] Заезд текста
    STATE_FX_MARQUEE,       // [NEW] Бегущая строка
    STATE_FX_PULSE,         // Дыхание
    STATE_FX_WAVE,          // Волна
    STATE_FX_SCANNER,       // Сканер
    STATE_FX_GLITCH,        // Глитч
    STATE_SHOW_TEXT,        // Текст статический
    STATE_FX_MORPH,         // Морфинг
    STATE_FX_DISSOLVE,      // Рассыпание
    STATE_FX_FADE,          // Fade Out -> Fade In
    STATE_MAX
} demo_state_t;

static demo_state_t g_state = STATE_CLOCK_NORMAL;
static absolute_time_t g_state_start_time;
static bool g_state_setup_done = false;

void set_demo_state(demo_state_t new_state) {
    g_state = new_state;
    g_state_start_time = get_absolute_time();
    g_state_setup_done = false;
}

void process_demo_sequence(void) {
    absolute_time_t now = get_absolute_time();
    uint32_t elapsed_ms = (uint32_t)(absolute_time_diff_us(g_state_start_time, now) / 1000);

    switch (g_state) {
        // --------------------------------------------------------------------
        // 1. Обычные часы (3 секунды)
        // --------------------------------------------------------------------
        case STATE_CLOCK_NORMAL:
            if (!g_state_setup_done) {
                display_set_dot_blinking(true); 
                display_set_brightness(255);    
                g_state_setup_done = true;
            }
            display_show_time(g_hour, g_min, true);

            // ПЕРЕХОД К SLIDE IN
            if (elapsed_ms > 3000) set_demo_state(STATE_FX_SLIDE_IN);
            break;

        // --------------------------------------------------------------------
        // 2. Slide In (Заезд текста "HI")
        // --------------------------------------------------------------------
        case STATE_FX_SLIDE_IN:
            if (!g_state_setup_done) {
                display_set_dot_blinking(false);
                // Скорость 200мс, текст "HI !"
                display_fx_slide_in("13.15", 200);
                g_state_setup_done = true;
            }
            
            // Ждем завершения эффекта + небольшая пауза, чтобы прочитать
            // Эффект сам остановится, но нам нужно переключить стейт
            if (elapsed_ms > 2000) set_demo_state(STATE_FX_MARQUEE);
            break;

        // --------------------------------------------------------------------
        // 3. Marquee (Бегущая строка)
        // --------------------------------------------------------------------
        case STATE_FX_MARQUEE:
            if (!g_state_setup_done) {
                // Длинный текст, скорость 180мс
                display_fx_marquee("192.168.1.4", 180);
                g_state_setup_done = true;
            }

            // Переходим дальше, когда эффект закончится
            if (!display_is_effect_running()) {
                set_demo_state(STATE_FX_PULSE);
            }
            break;

        // --------------------------------------------------------------------
        // 4. Pulse (Дыхание)
        // --------------------------------------------------------------------
        case STATE_FX_PULSE:
            if (!g_state_setup_done) {
                display_set_dot_blinking(true); // Вернули точки
                display_fx_pulse(4000); 
                g_state_setup_done = true;
            }
            display_show_time(g_hour, g_min, true); // Показываем время под эффектом

            if (elapsed_ms > 4500) set_demo_state(STATE_FX_WAVE);
            break;

        // --------------------------------------------------------------------
        // 5. Wave (Волна)
        // --------------------------------------------------------------------
        case STATE_FX_WAVE:
            if (!g_state_setup_done) {
                display_fx_wave(4000); 
                g_state_setup_done = true;
            }
            display_show_time(g_hour, g_min, true);

            if (elapsed_ms > 4500) set_demo_state(STATE_FX_SCANNER);
            break;

        // --------------------------------------------------------------------
        // 6. Scanner (KITT)
        // --------------------------------------------------------------------
        case STATE_FX_SCANNER:
            if (!g_state_setup_done) {
                display_fx_matrix(4000, 0); 
                g_state_setup_done = true;
            }
            display_show_time(g_hour, g_min, true);

            if (elapsed_ms > 4500) set_demo_state(STATE_FX_GLITCH);
            break;

        // --------------------------------------------------------------------
        // 7. Glitch
        // --------------------------------------------------------------------
        case STATE_FX_GLITCH:
            if (!g_state_setup_done) {
                display_fx_glitch(3000);
                g_state_setup_done = true;
            }
            display_show_time(g_hour, g_min, true);

            if (elapsed_ms > 3500) set_demo_state(STATE_SHOW_TEXT);
            break;

        // --------------------------------------------------------------------
        // 8. Текст COOL
        // --------------------------------------------------------------------
        case STATE_SHOW_TEXT:
            if (!g_state_setup_done) {
                display_set_dot_blinking(false);
                display_show_text("COOL");       
                g_state_setup_done = true;
            }
            
            if (elapsed_ms > 2000) set_demo_state(STATE_FX_MORPH);
            break;

        // --------------------------------------------------------------------
        // 9. Morph "COOL" -> "HEAT"
        // --------------------------------------------------------------------
        case STATE_FX_MORPH:
            if (!g_state_setup_done) {
                display_show_text("HEAT"); 
                vfd_seg_t target[4];
                vfd_seg_t *current = display_content_buffer();
                for(int i=0; i<4; i++) target[i] = current[i];

                display_show_text("COOL");
                display_fx_morph(2000, target, 20); 
                g_state_setup_done = true;
            }
            
            if (elapsed_ms > 2500) set_demo_state(STATE_FX_DISSOLVE);
            break;

        // --------------------------------------------------------------------
        // 10. Dissolve
        // --------------------------------------------------------------------
        case STATE_FX_DISSOLVE:
            if (!g_state_setup_done) {
                display_fx_dissolve(2000);
                g_state_setup_done = true;
            }

            if (elapsed_ms > 2500) set_demo_state(STATE_FX_FADE);
            break;

        // --------------------------------------------------------------------
        // 11. Fade Out / In
        // --------------------------------------------------------------------
        case STATE_FX_FADE:
            if (!g_state_setup_done) {
                display_fx_fade_out(1500);
                g_state_setup_done = true;
            }

            if (elapsed_ms > 1600 && elapsed_ms < 1700) {
                 display_show_time(g_hour, g_min, true);
            }

            if (elapsed_ms > 2000 && !display_is_effect_running()) {
                 display_fx_fade_in(1500);
                 g_state_start_time = now; 
                 g_state = STATE_MAX; 
            }
            break;

        case STATE_MAX:
             display_show_time(g_hour, g_min, true);
             if (elapsed_ms > 1600) {
                 set_demo_state(STATE_CLOCK_NORMAL);
             }
             break;
    }
}

/* ==========================================================================
   MAIN
   ========================================================================== */
int main() {
    stdio_init_all();
    sleep_ms(2000);
    
    // Инициализация
    display_init(DEMO_DIGIT_COUNT);
    display_set_brightness(255);
    display_set_auto_brightness(false); // Демонстрируем эффекты принудительно

    g_last_sec_time = get_absolute_time();

    while (true) {
        display_process();          // Крутит анимации
        update_clock_simulation();  // Считает секунды
        process_demo_sequence();    // Переключает режимы демо
        sleep_ms(5);
    }
    return 0;
}