#include <stdio.h>
#include "pico/stdlib.h"
#include "display_api.h"

/*
 * HL / FX TEST (новый)
 * --------------------
 * Что проверяем:
 * 1) Базовый HL-API:
 *    - display_init
 *    - display_show_number / _time / _date / _text
 *    - display_set_brightness
 *    - display_set_dot_blinking
 *
 * 2) FX-слой (перенесённый из легаси):
 *    - display_fx_fade_in
 *    - display_fx_fade_out
 *    - display_fx_pulse
 *    - display_fx_wave
 *    - display_fx_glitch
 *    - display_fx_matrix
 *
 * Везде display_process() вызывается из вспомогательной run_for_ms().
 */

static void run_for_ms(uint32_t total_ms, uint32_t step_ms)
{
    if (step_ms == 0) step_ms = 10;

    uint32_t ticks = total_ms / step_ms;
    for (uint32_t i = 0; i < ticks; i++) {
        display_process();
        sleep_ms(step_ms);
    }
}

int main(void)
{
    stdio_init_all();
    sleep_ms(500);

    // 4 разряда под твой текущий VFD
    display_init(4);

    display_set_brightness(255);
    display_set_dot_blinking(false);

    printf("HL/FX test: init done, starting demo...\n");

    // =========================
    // ФАЗА 1: HL — числа 0..9
    // =========================
    for (int32_t v = 0; v <= 9; v++) {
        display_show_number(v);
        run_for_ms(300, 30);   // показываем каждую цифру ~0.3s
    }

    // =========================
    // ФАЗА 2: HL — время и мигание точки
    // =========================

    // 2.1: статичное время 12:34 без мигания
    display_set_dot_blinking(false);
    display_show_time(12, 34, true);   // двоеточие включено, но не мигает
    run_for_ms(1500, 20);

    // 2.2: то же время, но с мигающей точкой
    display_set_dot_blinking(true);
    run_for_ms(4000, 20);              // несколько секунд наблюдаем мигание

    // Отключим мигание точки дальше, чтобы эффекты были «чистыми»
    display_set_dot_blinking(false);

    // =========================
    // ФАЗА 3: HL — дата 31.12
    // =========================
    display_show_date(31, 12);
    run_for_ms(2000, 20);

    // =========================
    // ФАЗА 4: HL — текст "1234"
    // =========================
    display_show_text("1234");
    run_for_ms(2000, 20);

    // =========================
    // ФАЗА 5: FX — яркостные эффекты на фоне времени
    // =========================
    uint8_t hours   = 12;
    uint8_t minutes = 0;

    // Базовое время: 12:00, без мигания, просто чтобы был понятный контент
    display_show_time(hours, minutes, true);
    display_set_brightness(255);

    // 5.1: FADE OUT — плавное выключение за 1.5 с
    display_fx_fade_out(1500);
    run_for_ms(2000, 20);  // чуть дольше длительности, чтобы эффект гарантированно завершился

    // 5.2: FADE IN — плавное включение за 1.5 с
    display_set_brightness(0);  // стартуем из 0
    display_show_time(hours, minutes, true);
    display_fx_fade_in(1500);
    run_for_ms(2000, 20);

    // 5.3: PULSE — «дыхание» на 3 секунды
    display_set_brightness(255);
    display_show_time(hours, minutes, true);
    display_fx_pulse(3000);
    run_for_ms(3500, 20);

    // 5.4: WAVE — волна яркости по разрядам на 3 секунды
    display_set_brightness(255);
    display_show_time(hours, minutes, true);
    display_fx_wave(3000);
    run_for_ms(3500, 20);

    // 5.5: GLITCH — динамический фликер на 3 секунды
    display_set_brightness(255);
    display_show_time(hours, minutes, true);
    display_fx_glitch(3000);
    run_for_ms(3500, 20);

    // 5.6: MATRIX — "дождь" по яркости разрядов на 3 секунды
    display_set_brightness(255);
    display_show_time(hours, minutes, true);
    display_fx_matrix(3000, 50);   // ~20 FPS
    run_for_ms(3500, 20);

    // =========================
    // ФАЗА 6: простой «софт RTC» + FX в фоне
    // =========================
    display_set_brightness(255);
    display_set_dot_blinking(true);


    // =========================
    // ФАЗА 7: OVERLAY — boot / wifi / ntp
    // =========================
    display_set_dot_blinking(false);   // чтобы точка не мешала анимациям

    display_show_time(12, 34, true);
    run_for_ms(1000, 20);

    // boot-анимация: 0000 → 1111 → ... → 9999
    display_overlay_boot(0);
    run_for_ms(3000, 20);

    // после boot вернётся старый контент, обновим его явно
    display_show_time(12, 34, true);
    run_for_ms(500, 20);

    // wifi-анимация: мигающие "8888"
    display_overlay_wifi(0);
    run_for_ms(3000, 20);

    display_show_time(12, 34, true);
    run_for_ms(500, 20);

    // ntp-анимация: бегущая "8" туда-сюда
    display_overlay_ntp(0);
    run_for_ms(4000, 20);

    // вернулись к обычному виду и уходим в вечный цикл «часов»
    display_show_time(12, 0, true);
    display_set_dot_blinking(true);





    hours   = 12;
    minutes = 0;

    uint32_t tick = 0;

    while (true) {
        // Раз в "секунду" (100 тиков * 10 ms = 1 s) — обновляем минуты
        if ((tick % 100) == 0) {
            minutes++;
            if (minutes >= 60) {
                minutes = 0;
                hours = (uint8_t)((hours + 1) % 24);
            }
            display_show_time(hours, minutes, true);

            // Каждые 5 минут — небольшой pulse на яркости
            if ((minutes % 5) == 0) {
                (void)display_fx_pulse(2000);
            }

            // Каждые 15 минут — короткий glitch
            if ((minutes % 15) == 0) {
                (void)display_fx_glitch(1500);
            }
        }

        display_process();
        sleep_ms(10);
        tick++;
    }
}
