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
    STATE_FX_PULSE,         // Дыхание (прозрачный)
    STATE_FX_WAVE,          // Волна (прозрачный) -> ДОБАВЛЕНО
    STATE_FX_SCANNER,       // Сканер/KITT (бывшая Matrix, прозрачный)
    STATE_FX_GLITCH,        // Глитч (блокирующий)
    STATE_SHOW_TEXT,        // Текст
    STATE_FX_MORPH,         // Морфинг
    STATE_FX_DISSOLVE,      // Рассыпание
    STATE_FX_FADE,          // Fade Out -> Wait -> Fade In
    STATE_MAX
} demo_state_t;

static demo_state_t g_state = STATE_CLOCK_NORMAL;
static absolute_time_t g_state_start_time;
static bool g_state_setup_done = false;

void set_demo_state(demo_state_t new_state) {
    g_state = new_state;
    g_state_start_time = get_absolute_time();
    g_state_setup_done = false;
    // printf(">>> DEMO STATE: %d\n", g_state); // Раскомментируй для отладки в USB UART
}

void process_demo_sequence(void) {
    absolute_time_t now = get_absolute_time();
    uint32_t elapsed_ms = (uint32_t)(absolute_time_diff_us(g_state_start_time, now) / 1000);

    switch (g_state) {
        // --------------------------------------------------------------------
        // 1. Обычные часы (4 секунды)
        // --------------------------------------------------------------------
        case STATE_CLOCK_NORMAL:
            if (!g_state_setup_done) {
                display_set_dot_blinking(true); // Точки мигают
                display_set_brightness(255);    // Полная яркость
                g_state_setup_done = true;
            }
            display_show_time(g_hour, g_min, true);

            if (elapsed_ms > 4000) set_demo_state(STATE_FX_PULSE);
            break;

        // --------------------------------------------------------------------
        // 2. Pulse (Дыхание) - 5 секунд
        // Яркость плавно дышит всем экраном. Часы идут.
        // --------------------------------------------------------------------
        case STATE_FX_PULSE:
            if (!g_state_setup_done) {
                display_fx_pulse(5000); 
                g_state_setup_done = true;
            }
            display_show_time(g_hour, g_min, true);

            if (elapsed_ms > 5500) set_demo_state(STATE_FX_WAVE);
            break;

        // --------------------------------------------------------------------
        // 3. Wave (Волна) - 5 секунд [НОВЫЙ]
        // Яркость переливается слева направо. Часы идут.
        // --------------------------------------------------------------------
        case STATE_FX_WAVE:
            if (!g_state_setup_done) {
                display_fx_wave(5000); 
                g_state_setup_done = true;
            }
            display_show_time(g_hour, g_min, true);

            if (elapsed_ms > 5500) set_demo_state(STATE_FX_SCANNER);
            break;

        // --------------------------------------------------------------------
        // 4. Scanner (KITT) - 5 секунд
        // Бегающий огонек. Часы идут.
        // --------------------------------------------------------------------
        case STATE_FX_SCANNER:
            if (!g_state_setup_done) {
                // Вызываем matrix, который мы переделали в сканер
                display_fx_matrix(5000, 0); 
                g_state_setup_done = true;
            }
            display_show_time(g_hour, g_min, true);

            if (elapsed_ms > 5500) set_demo_state(STATE_FX_GLITCH);
            break;

        // --------------------------------------------------------------------
        // 5. Glitch - 3 секунды
        // Цифры прыгают и сбоят.
        // --------------------------------------------------------------------
        case STATE_FX_GLITCH:
            if (!g_state_setup_done) {
                display_fx_glitch(3000);
                g_state_setup_done = true;
            }
            // Контент обновлять не обязательно (он блокируется), но можно
            display_show_time(g_hour, g_min, true);

            if (elapsed_ms > 3500) set_demo_state(STATE_SHOW_TEXT);
            break;

        // --------------------------------------------------------------------
        // 6. Текст COOL (2 секунды)
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
        // 7. Morph "COOL" -> "HEAT" (2 секунды)
        // --------------------------------------------------------------------
        case STATE_FX_MORPH:
            if (!g_state_setup_done) {
                // 1. Показываем HEAT в буфер (но не пушим в LL, если бы делали руками)
                // Но проще просто подготовить массив сегментов
                display_show_text("HEAT"); 
                
                vfd_seg_t target[4];
                vfd_seg_t *current = display_content_buffer();
                for(int i=0; i<4; i++) target[i] = current[i];

                // 2. Возвращаем исходное слово COOL
                display_show_text("COOL");

                // 3. Запускаем превращение
                display_fx_morph(2000, target, 20); 
                g_state_setup_done = true;
            }
            
            if (elapsed_ms > 2500) set_demo_state(STATE_FX_DISSOLVE);
            break;

        // --------------------------------------------------------------------
        // 8. Dissolve (Рассыпание) - 2 секунды
        // Эффектно рассыпаем слово HEAT
        // --------------------------------------------------------------------
        case STATE_FX_DISSOLVE:
            if (!g_state_setup_done) {
                // Можно рассыпать HEAT, или показать "8888" для эффектности
                // display_show_text("8888"); 
                display_fx_dissolve(2000);
                g_state_setup_done = true;
            }

            if (elapsed_ms > 2500) set_demo_state(STATE_FX_FADE);
            break;

        // --------------------------------------------------------------------
        // 9. Fade Out / In (Полный цикл)
        // --------------------------------------------------------------------
        case STATE_FX_FADE:
            if (!g_state_setup_done) {
                // Гасим экран
                display_fx_fade_out(1500);
                g_state_setup_done = true;
            }

            // В середине темноты подменяем контент обратно на часы
            if (elapsed_ms > 1600 && elapsed_ms < 1700) {
                 display_show_time(g_hour, g_min, true);
            }

            // Когда погасло (1.5с) + пауза (0.5с), включаем обратно
            if (elapsed_ms > 2000 && !display_is_effect_running()) {
                 display_fx_fade_in(1500);
                 
                 // Хак для стейт-машины: переходим в ожидание, сбросив таймер
                 g_state_start_time = now; 
                 g_state = STATE_MAX; 
            }
            break;

        case STATE_MAX:
             display_show_time(g_hour, g_min, true);
             if (elapsed_ms > 1600) {
                 // Круг замкнулся
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