// #include "display.h"
// #include "pico/stdlib.h"
// #include "hardware/rtc.h"
// #include "logging.h"

// //
// // =======================================================================
// //  Вспомогательные утилиты
// // =======================================================================
// //

// static void on_effect_complete(void) {
//     LOG_INFO("Effect completed (on_effect_complete callback)");
// }

// static void banner(const char *title) {
//     LOG_INFO("------------------------------------------------------------");
//     LOG_INFO("%s", title);
//     LOG_INFO("------------------------------------------------------------");
// }

// static void hold_ms(uint32_t ms) {
//     sleep_ms(ms);
// }

// // Ждём, пока эффект сам закончится (g_display_mode вернётся в TIME),
// // либо по таймауту насильно стопаем эффекты.
// static bool wait_for_effect(uint32_t timeout_ms) {
//     uint32_t start_us = time_us_32();
//     while (g_display_mode == DISPLAY_MODE_EFF) {
//         uint32_t elapsed_ms = (time_us_32() - start_us) / 1000;
//         if (elapsed_ms > timeout_ms) {
//             LOG_ERROR("Effect timeout after %u ms, forcing stop", timeout_ms);
//             display_stop_all_effects();
//             return false;
//         }
//         sleep_ms(10);
//     }
//     return true;
// }

// static void dump_buffer(const char *label) {
//     const volatile uint8_t *buf = display_get_buffer();
//     LOG_INFO("%s buffer: [%u %u %u %u]",
//              label, buf[0], buf[1], buf[2], buf[3]);
// }

// //
// // =======================================================================
// //  Тесты печати и буфера
// // =======================================================================
// //

// static void test_print_and_buffer(void) {
//     banner("TEST 1: print() + get_buffer()");

//     display_print("1234");
//     hold_ms(500);
//     dump_buffer("After print(\"1234\")");

//     display_print("0000");
//     hold_ms(500);
//     dump_buffer("After print(\"0000\")");
// }

// static void test_print_digital(void) {
//     banner("TEST 2: print_digital()");

//     display_print_digital(42);
//     LOG_INFO("print_digital(42)");
//     hold_ms(600);

//     display_print_digital(-17);
//     LOG_INFO("print_digital(-17)");
//     hold_ms(600);

//     display_print_digital(2025);
//     LOG_INFO("print_digital(2025)");
//     hold_ms(600);
// }

// static void test_error_output(void) {
//     banner("TEST 3: display_error() 0..9");

//     for (uint8_t code = 0; code <= 9; code++) {
//         display_error(code);
//         LOG_INFO("display_error(%u)", code);
//         hold_ms(400);
//     }
// }

// static void test_scrolling(void) {
//     banner("TEST 4: display_scrolling_digits()");

//     // строка длинее количества разрядов
//     display_scrolling_digits("0123456789");
//     LOG_INFO("Started scrolling \"0123456789\"");
//     // Скролл сам вернётся в TIME через scroll_cb (см. display.c)
//     wait_for_effect(10000);
// }

// static void test_morph(void) {
//     banner("TEST 5: display_morph_to()");

//     display_print("0000");
//     hold_ms(300);

//     if (display_morph_to(1234, 3000, 20)) {
//         LOG_INFO("display_morph_to(1234, 3000, 20) started");
//         wait_for_effect(4000);
//     } else {
//         LOG_ERROR("display_morph_to() returned false");
//     }
// }

// //
// // =======================================================================
// //  Тесты эффектов
// // =======================================================================
// //

// static void test_effect_boot(void) {
//     banner("TEST 6: effect_boot_animation()");

//     if (display_effect_boot_animation(2000)) {
//         LOG_INFO("Boot animation started");
//         wait_for_effect(2500);
//     } else {
//         LOG_ERROR("Boot animation failed to start");
//     }
// }

// static void test_effect_connecting_wifi(void) {
//     banner("TEST 7: effect_connecting_wifi()");

//     if (display_effect_connecting_wifi(2000)) {
//         LOG_INFO("Wi-Fi connecting effect started");
//         wait_for_effect(2500);
//     } else {
//         LOG_ERROR("Wi-Fi connecting effect failed to start");
//     }
// }

// static void test_effect_ntp_sync(void) {
//     banner("TEST 8: effect_ntp_sync()");

//     if (display_effect_ntp_sync(2000)) {
//         LOG_INFO("NTP sync effect started");
//         wait_for_effect(2500);
//     } else {
//         LOG_ERROR("NTP sync effect failed to start");
//     }
// }

// static void test_effect_fade(void) {
//     banner("TEST 9: fade_out() + fade_in()");

//     if (display_effect_fade_out(1500)) {
//         LOG_INFO("Fade out started");
//         wait_for_effect(2000);
//     } else {
//         LOG_ERROR("Fade out failed to start");
//     }

//     hold_ms(300);

//     if (display_effect_fade_in(1500)) {
//         LOG_INFO("Fade in started");
//         wait_for_effect(2000);
//     } else {
//         LOG_ERROR("Fade in failed to start");
//     }
// }

// static void test_effect_pulse(void) {
//     banner("TEST 10: effect_pulse()");

//     if (display_effect_pulse(4000, 4)) {
//         LOG_INFO("Pulse effect started (4 cycles, 4000 ms)");
//         wait_for_effect(5000);
//     } else {
//         LOG_ERROR("Pulse effect failed to start");
//     }
// }

// static void test_effect_wave(void) {
//     banner("TEST 11: effect_brightness_wave()");

//     if (display_effect_brightness_wave(5000)) {
//         LOG_INFO("Wave effect started (5000 ms)");
//         wait_for_effect(6000);
//     } else {
//         LOG_ERROR("Wave effect failed to start");
//     }
// }

// static void test_effect_dynamic_flicker(void) {
//     banner("TEST 12: effect_dynamic_flicker() + flicker_triggered()");

//     // Dynamic flicker работает в режиме TIME, сам не переводит в DISPLAY_MODE_EFF.
//     // Но всё равно тестируем включение/отключение и факт срабатывания.
//     bool ok = display_effect_dynamic_flicker(1000, 3000, 500);
//     if (!ok) {
//         LOG_ERROR("display_effect_dynamic_flicker() returned false");
//         return;
//     }
//     LOG_INFO("Dynamic flicker started (min=1000, max=3000, duration=500)");

//     // Ждём немного, пока хотя бы несколько раз мигнёт
//     hold_ms(6000);

//     if (display_flicker_triggered()) {
//         LOG_INFO("Flicker has been triggered at least once");
//     } else {
//         LOG_WARN("Flicker has NOT been triggered");
//     }

//     display_effect_stop_flicker();
//     LOG_INFO("Dynamic flicker stopped");
//     hold_ms(500);
// }

// //
// // =======================================================================
// //  Яркость и режимы
// // =======================================================================
// //

// static void test_brightness_manual_and_per_digit(void) {
//     banner("TEST 13: manual brightness + per-digit brightness");

//     // Включаем ручную яркость
//     display_enable_manual_brightness(true);
//     LOG_INFO("Manual brightness enabled");

//     for (int b = 10; b <= 100; b += 30) {
//         display_set_brightness(b);
//         LOG_INFO("Global brightness set to %d", b);
//         display_print("1111");
//         hold_ms(500);
//     }

//     // Тест покомбовой яркости
//     uint8_t levels1[4] = {10, 40, 70, 100};
//     if (display_set_digit_brightness(levels1)) {
//         LOG_INFO("Per-digit brightness set to [10,40,70,100]");
//         display_print("1234");
//         hold_ms(1500);
//     } else {
//         LOG_ERROR("display_set_digit_brightness() returned false");
//     }

//     uint8_t levels2[4] = {100, 70, 40, 10};
//     if (display_set_digit_brightness(levels2)) {
//         LOG_INFO("Per-digit brightness set to [100,70,40,10]");
//         display_print("5678");
//         hold_ms(1500);
//     }

//     // Возвращаем все разряды к максимальной локальной яркости
//     uint8_t levels_max[4] = {100, 100, 100, 100};
//     display_set_digit_brightness(levels_max);
//     LOG_INFO("Per-digit brightness reset to [100,100,100,100]");

//     // Выключаем ручную яркость, возвращаем управление алгоритмам
//     display_enable_manual_brightness(false);
//     LOG_INFO("Manual brightness disabled");
// }

// static void test_dot_blinking(void) {
//     banner("TEST 14: dot blinking");

//     display_print("1212");
//     display_set_dot_blinking(true);
//     LOG_INFO("Dot blinking enabled");
//     hold_ms(3000);

//     display_set_dot_blinking(false);
//     LOG_INFO("Dot blinking disabled");
//     hold_ms(1000);
// }

// static void test_night_and_adaptive(void) {
//     banner("TEST 15: night mode + adaptive brightness");

//     // Установим время глубокой ночи
//     datetime_t night = {
//         .year = 2025, .month = 11, .day = 24,
//         .dotw = 1,
//         .hour = 2, .min = 0, .sec = 0
//     };
//     rtc_set_datetime(&night);

//     display_enable_night_mode(true);
//     LOG_INFO("Night mode enabled, time set to 02:00");

//     hold_ms(2000);

//     // Включаем адаптивную яркость (использует ADC)
//     display_enable_adaptive_brightness(true);
//     LOG_INFO("Adaptive brightness enabled (ADC-based)");

//     hold_ms(3000);

//     // Отключаем всё, возвращаемся в нормальное состояние
//     display_enable_adaptive_brightness(false);
//     display_enable_night_mode(false);
//     LOG_INFO("Night mode & adaptive brightness disabled");
// }

// //
// // =======================================================================
// //  Режим времени
// // =======================================================================
// //

// static void test_time_mode(void) {
//     banner("TEST 16: time mode / display_start_time_updates()");

//     datetime_t t = {
//         .year = 2025,
//         .month = 11,
//         .day = 24,
//         .dotw = 1,
//         .hour = 10,
//         .min = 20,
//         .sec = 0
//     };

//     rtc_set_datetime(&t);

//     if (!display_start_time_updates()) {
//         LOG_ERROR("display_start_time_updates() returned false");
//         return;
//     }
//     LOG_INFO("Time updates started, watching clock for few seconds...");
//     hold_ms(5000);

//     dump_buffer("Time mode buffer");
// }

// //
// // =======================================================================
// //  MAIN
// // =======================================================================
// //

// int main(void) {
//     stdio_init_all();
//     sleep_ms(500); // чуть подождать, чтобы USB-UART успел подняться

//     LOG_INFO("============================================================");
//     LOG_INFO("             VFD DISPLAY FULL FUNCTION TEST SUITE           ");
//     LOG_INFO("============================================================");

//     // Инициализация RTC (используется в display.c для времени и яркости)
//     rtc_init();

//     // Конфигурация дисплея — ПРИ НЕОБХОДИМОСТИ поправь под свои пины
//     display_config_t cfg = {
//         .data_pin          = 15,
//         .clock_pin         = 14,
//         .latch_pin         = 13,
//         .light_sensor_adc  = 26,     // ADC 0
//         .digit_count       = 4,
//         .fast_refresh_rate = 120,    // Гц
//         .night_start_hour  = 22,
//         .night_end_hour    = 7,
//         .on_effect_complete = on_effect_complete,
//     };

//     if (!display_init(&cfg)) {
//         LOG_ERROR("Display init FAILED");
//         while (true) {
//             sleep_ms(1000);
//         }
//     }
//     LOG_INFO("Display initialized OK");

//     if (display_is_initialized()) {
//         LOG_INFO("display_is_initialized() == true");
//     } else {
//         LOG_WARN("display_is_initialized() == false (unexpected)");
//     }

//     // Немного базовой информации
//     dump_buffer("Initial buffer");

//     // ----------------- Тесты печати и буфера -----------------
//     test_print_and_buffer();
//     test_print_digital();
//     test_error_output();
//     test_scrolling();
//     test_morph();

//     // ----------------- Тесты эффектов -----------------
//     test_effect_boot();
//     test_effect_connecting_wifi();
//     test_effect_ntp_sync();
//     test_effect_fade();
//     test_effect_pulse();
//     test_effect_wave();
//     test_effect_dynamic_flicker();

//     // На всякий случай стопаем всё, что могло остаться
//     display_stop_all_effects();

//     // ----------------- Яркость и точка -----------------
//     test_brightness_manual_and_per_digit();
//     test_dot_blinking();
//     test_night_and_adaptive();

//     // ----------------- Режим времени -----------------
//     test_time_mode();

//     LOG_INFO("============================================================");
//     LOG_INFO("              ALL DISPLAY TESTS HAVE BEEN RUN               ");
//     LOG_INFO("============================================================");

//     // В финале оставляем часы работать
//     display_enable_night_mode(false);
//     display_enable_adaptive_brightness(false);
//     display_enable_manual_brightness(false);
//     display_set_brightness(DISPLAY_BRIGHTNESS_MAX);
//     display_set_dot_blinking(true);

//     LOG_INFO("Entering idle loop with time mode active...");
//     while (true) {
//         sleep_ms(1000);
//     }

//     return 0;
// }


#include "display.h"
#include "pico/stdlib.h"
#include "hardware/rtc.h"
#include "logging.h"

// Колбэк, который драйвер вызывает по завершении эффекта
static void on_effect_complete(void) {
    LOG_INFO("Effect completed (callback)");
}

// Ожидаем завершения эффекта с таймаутом
static bool wait_for_effect(uint32_t timeout_ms) {
    uint32_t start = time_us_32();
    while (g_display_mode == DISPLAY_MODE_EFF) {
        uint32_t now = time_us_32();
        uint32_t elapsed_ms = (now - start) / 1000;
        if (elapsed_ms > timeout_ms) {
            LOG_ERROR("Effect timeout after %u ms", timeout_ms);
            display_stop_all_effects();
            return false;
        }
        tight_loop_contents();
    }
    return true;
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000); // немного подождём, чтобы USB/лог успели подняться

    LOG_INFO("VFD clock 24h stress test starting...");

    // --- Инициализация и установка времени RTC ---
    rtc_init();
    datetime_t t = {
        .year  = 2025,
        .month = 1,
        .day   = 1,
        .dotw  = 3,   // не критично
        .hour  = 1,
        .min   = 16,
        .sec   = 0
    };
    rtc_set_datetime(&t);
    LOG_INFO("RTC initial time set to %02d:%02d", t.hour, t.min);

    // --- Конфигурация дисплея ---
    display_config_t config = {
        .data_pin           = 15,
        .clock_pin          = 14,
        .latch_pin          = 13,
        .light_sensor_adc   = 26,
        .digit_count        = 4,
        .fast_refresh_rate  = 120,
        .night_start_hour   = 21,
        .night_end_hour     = 6,
        .on_effect_complete = on_effect_complete
    };

    if (!display_init(&config)) {
        LOG_ERROR("Display initialization FAILED");
        while (true) {
            sleep_ms(1000);
        }
    }
    LOG_INFO("Display initialized");

    // Ручной режим яркости на максимум (для теста)
    display_enable_night_mode(false);
    display_enable_adaptive_brightness(false);
    display_enable_manual_brightness(true);
    display_set_brightness(DISPLAY_BRIGHTNESS_MAX);

    // --- Плавное разгорание при подаче питания ---
    LOG_INFO("Warm-up: blank screen 2 s");
    // По умолчанию буфер пустой, просто ждём пару секунд
    sleep_ms(2000);

    LOG_INFO("Warm-up: fade in");
    if (!display_effect_fade_in(2000)) {
        LOG_ERROR("Fade-in start FAILED");
    } else {
        wait_for_effect(3000);
    }

    // --- Запускаем показ времени и мигание точки ---
    if (!display_start_time_updates()) {
        LOG_WARN("Time updates were not started (maybe already active)");
    }
    display_set_dot_blinking(true);

    // --- Динамический flicker поверх всего с большим периодом ---
    // Фликер срабатывает только в режиме DISPLAY_MODE_TIME, так что
    // когда идут часовые/получасовые эффекты, он сам по себе "приподымается".
    if (!display_effect_dynamic_flicker(5 * 60 * 1000, 15 * 60 * 1000, 400)) {
        LOG_WARN("Dynamic flicker not started");
    } else {
        LOG_INFO("Dynamic flicker started");
    }

    int last_min  = -1;
    int last_hour = -1;

    LOG_INFO("Entering main loop...");

    while (true) {
        if (rtc_running()) {
            datetime_t now;
            rtc_get_datetime(&now);

            if (last_min < 0) {
                last_min  = now.min;
                last_hour = now.hour;
            }

            if (now.min != last_min) {
                int prev_min  = last_min;
                int prev_hour = last_hour;

                last_min  = now.min;
                last_hour = now.hour;

                // Переход 59 -> 00: часовой эффект
                if (prev_min == 59 && now.min == 0) {
                    LOG_INFO("Hour change %02d:%02d -> %02d:%02d, running HOUR effect",
                             prev_hour, prev_min, now.hour, now.min);

                    if (g_display_mode == DISPLAY_MODE_TIME) {
                        if (!display_effect_ntp_sync(1500)) {
                            LOG_ERROR("Hour effect (NTP sync) start FAILED");
                        } else {
                            wait_for_effect(3000);
                        }
                    } else {
                        LOG_WARN("Skipped HOUR effect: display is busy (mode=%d)", g_display_mode);
                    }
                }
                // Переход 29 -> 30: получасовой эффект
                else if (prev_min == 29 && now.min == 30) {
                    LOG_INFO("Half-hour reached %02d:%02d, running HALF-HOUR effect",
                             now.hour, now.min);

                    if (g_display_mode == DISPLAY_MODE_TIME) {
                        if (!display_effect_brightness_wave(2000)) {
                            LOG_ERROR("Half-hour effect (wave) start FAILED");
                        } else {
                            wait_for_effect(3000);
                        }
                    } else {
                        LOG_WARN("Skipped HALF-HOUR effect: display is busy (mode=%d)", g_display_mode);
                    }
                }
            }
        }

        // Частота опроса ~5 Гц — более чем достаточно, чтобы не пропускать переходы минут
        sleep_ms(200);
    }

    return 0;
}

