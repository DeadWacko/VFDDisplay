#include <stdio.h>
#include "pico/stdlib.h"
#include "display_api.h"
#include "display_font.h" 
/*
 * HL / FX / OVERLAY TEST
 * ----------------------
 * Что проверяем:
 *
 * 1) Базовый HL-API:
 *    - display_init
 *    - display_show_number / _time / _date / _text
 *    - display_set_brightness
 *    - display_set_dot_blinking
 *
 * 2) FX-слой:
 *    - display_fx_fade_in
 *    - display_fx_fade_out
 *    - display_fx_pulse
 *    - display_fx_wave
 *    - display_fx_glitch
 *    - display_fx_matrix
 *
 * 3) Overlay-слой:
 *    - display_overlay_boot
 *    - display_overlay_wifi
 *    - display_overlay_ntp
 *    - display_overlay_stop
 *
 * 4) Статусы и буфер:
 *    - display_get_mode
 *    - display_is_effect_running
 *    - display_is_overlay_running
 *    - display_content_buffer
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

/* Простая печать текущего режима для отладки */
static void log_mode(const char *tag)
{
    display_mode_t mode = display_get_mode();
    bool eff = display_is_effect_running();
    bool ovl = display_is_overlay_running();

    printf("[%s] mode=%d, effect=%d, overlay=%d\n",
           tag, (int)mode, eff ? 1 : 0, ovl ? 1 : 0);
}

int main(void)
{
    stdio_init_all();
    sleep_ms(500);

    // 4 разряда под текущий VFD
    display_init(4);

    display_set_brightness(255);
    display_set_dot_blinking(false);

    printf("HL/FX/OVERLAY test: init done, starting demo...\n");
    log_mode("after init");

    // =========================
    // ФАЗА 1: HL — числа 0..9
    // =========================
    printf("Phase 1: numbers 0..9\n");
    for (int32_t v = 0; v <= 9; v++) {
        display_show_number(v);
        run_for_ms(300, 30);   // показываем каждую цифру ~0.3s
    }

    // =========================
    // ФАЗА 2: HL — время и мигание точки
    // =========================

    printf("Phase 2: time + dot blinking\n");

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
    printf("Phase 3: date 31.12\n");
    display_show_date(31, 12);
    run_for_ms(2000, 20);

    // =========================
    // ФАЗА 4: HL — текст "1234" + проверка display_content_buffer()
    // =========================
    printf("Phase 4: text \"1234\" + content buffer\n");
    display_show_text("1234");
    run_for_ms(1000, 20);

    // Проверим, что display_content_buffer() возвращает что-то осмысленное
    vfd_seg_t *buf = display_content_buffer();
    if (buf) {
        printf("CONTENT buffer[0] = 0x%02X\n", (unsigned int)buf[0]);
    } else {
        printf("CONTENT buffer is NULL (unexpected)\n");
    }
    run_for_ms(1000, 20);

    // =========================
    // ФАЗА 5: FX — яркостные эффекты на фоне времени
    // =========================
    printf("Phase 5: FX effects\n");

    uint8_t hours   = 12;
    uint8_t minutes = 0;

    // Базовое время: 12:00, без мигания, просто чтобы был понятный контент
    display_show_time(hours, minutes, true);
    display_set_brightness(255);
    log_mode("before FX");

    // 5.1: FADE OUT — плавное выключение за 1.5 с
    printf(" FX: fade out\n");
    display_fx_fade_out(1500);
    log_mode("fade out start");
    run_for_ms(2000, 20);  // чуть дольше длительности, чтобы эффект гарантированно завершился
    log_mode("fade out end");

    // 5.2: FADE IN — плавное включение за 1.5 с
    printf(" FX: fade in\n");
    display_set_brightness(0);  // стартуем из 0
    display_show_time(hours, minutes, true);
    display_fx_fade_in(1500);
    log_mode("fade in start");
    run_for_ms(2000, 20);
    log_mode("fade in end");

    // 5.3: PULSE — «дыхание» на 3 секунды
    printf(" FX: pulse\n");
    display_set_brightness(255);
    display_show_time(hours, minutes, true);
    display_fx_pulse(3000);
    log_mode("pulse start");
    run_for_ms(3500, 20);
    log_mode("pulse end");

    // 5.4: WAVE — волна яркости по разрядам на 3 секунды
    printf(" FX: wave\n");
    display_set_brightness(255);
    display_show_time(hours, minutes, true);
    display_fx_wave(3000);
    log_mode("wave start");
    run_for_ms(3500, 20);
    log_mode("wave end");

    // 5.5: GLITCH — динамический фликер на 3 секунды
    printf(" FX: glitch\n");
    display_set_brightness(255);
    display_show_time(hours, minutes, true);
    display_fx_glitch(3000);
    log_mode("glitch start");
    run_for_ms(3500, 20);
    log_mode("glitch end");

    // 5.6: MATRIX — "дождь" по яркости разрядов на 3 секунды
    printf(" FX: matrix rain\n");
    display_set_brightness(255);
    display_show_time(hours, minutes, true);
    display_fx_matrix(3000, 50);   // ~20 FPS
    log_mode("matrix start");
    run_for_ms(3500, 20);
    log_mode("matrix end");


    // 5.7: MORPH — плавный переход 12:34 -> 88:88
    printf(" FX: morph 1234 -> 8888\n");
    vfd_seg_t tgt[4] = {
    g_display_font_digits[8],
    g_display_font_digits[8],
    g_display_font_digits[8],
    g_display_font_digits[8]
    };
    display_fx_morph(2000, tgt, 12);
    run_for_ms(2500, 20);

    // 5.8: DISSOLVE — рассыпание сегментов
    printf(" FX: dissolve\n");
    display_show_time(12, 34, true);
    display_fx_dissolve(2000);
    run_for_ms(2500, 20);





    // =========================
    // ФАЗА 6: ЯРКОСТЬ — ночной/авто режимы (smoke test)
    // =========================
    printf("Phase 6: brightness modes (night/auto)\n");

    // 6.1: Ночной режим включён/выключен
    printf(" Night mode ON\n");
    display_set_night_mode(true);
    run_for_ms(1000, 20);
    printf(" Night mode OFF\n");
    display_set_night_mode(false);
    run_for_ms(1000, 20);

    // 6.2: Автояркость включена/выключена
    // (на стенде без настоящего датчика это просто проверка, что ничего не падает)
    printf(" Auto brightness ON\n");
    display_set_auto_brightness(true);
    run_for_ms(1000, 20);
    printf(" Auto brightness OFF\n");
    display_set_auto_brightness(false);
    run_for_ms(1000, 20);

    // Вернём нормальное состояние
    display_set_brightness(255);
    display_show_time(12, 34, true);

    // =========================
    // ФАЗА 7: OVERLAY — boot / wifi / ntp + overlay_stop
    // =========================
    printf("Phase 7: overlays (boot / wifi / ntp / stop)\n");

    display_set_dot_blinking(false);   // чтобы точка не мешала анимациям

    display_show_time(12, 34, true);
    run_for_ms(1000, 20);
    log_mode("before boot overlay");

    // 7.1 boot-анимация: 0000 → 1111 → ... → 9999
    printf(" Overlay: boot\n");
    display_overlay_boot(0);   // duration_ms сейчас не используется
    log_mode("boot start");
    run_for_ms(3000, 20);
    log_mode("boot end");

    // после boot вернётся старый контент, обновим его явно
    display_show_time(12, 34, true);
    run_for_ms(500, 20);

    // 7.2 wifi-анимация: мигающие "8888"
    printf(" Overlay: wifi (with explicit stop)\n");
    display_overlay_wifi(0);
    log_mode("wifi start");
    run_for_ms(1500, 20);

    // Проверяем явный overlay_stop()
    printf(" Overlay: wifi stop()\n");
    display_overlay_stop();
    log_mode("wifi stopped");
    run_for_ms(500, 20);

    display_show_time(12, 34, true);
    run_for_ms(500, 20);

    // 7.3 ntp-анимация: бегущая "8" туда-сюда
    printf(" Overlay: ntp\n");
    display_overlay_ntp(0);
    log_mode("ntp start");
    run_for_ms(4000, 20);
    log_mode("ntp end");

    // вернулись к обычному виду и уходим в вечный цикл «часов»
    display_show_time(12, 0, true);
    display_set_dot_blinking(true);
    log_mode("before clock loop");

    // =========================
    // ФАЗА 8: "софт RTC" + FX в фоне
    // =========================
    printf("Phase 8: soft RTC loop with periodic FX\n");

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
                printf(" [loop] pulse at %02u:%02u\n", hours, minutes);
            }

            // Каждые 15 минут — короткий glitch
            if ((minutes % 15) == 0) {
                (void)display_fx_glitch(1500);
                printf(" [loop] glitch at %02u:%02u\n", hours, minutes);
            }
        }

        display_process();
        sleep_ms(10);
        tick++;
    }
}
