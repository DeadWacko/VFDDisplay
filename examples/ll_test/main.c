#include <stdio.h>
#include "pico/stdlib.h"
#include "display_ll.h"

/*
 * Конфигурация пинов.
 * Должна совпадать с вашим физическим подключением к 74HC595.
 */
#define VFD_DATA_PIN      15
#define VFD_CLOCK_PIN     14
#define VFD_LATCH_PIN     13

/* Параметры дисплея */
#define VFD_DIGIT_COUNT   4
#define VFD_REFRESH_HZ    120

/*
 * Простая таблица сегментов (0-9) для 7-сегментного индикатора.
 * A=bit0, B=bit1, ..., G=bit6, DP=bit7
 */
static const vfd_seg_t FONT_DIGITS[10] = {
    0b01111011, // 0
    0b00000011, // 1
    0b01011110, // 2
    0b01001111, // 3
    0b00100111, // 4
    0b01101101, // 5
    0b01111101, // 6
    0b01000011, // 7
    0b01111111, // 8
    0b01101111  // 9
};

/* Вспомогательная функция вывода цифры */
static void test_show_digit(uint8_t pos, uint8_t number)
{
    if (number > 9) return;
    vfd_seg_t segmask = FONT_DIGITS[number];
    display_ll_set_digit_raw(pos, segmask);
}

/* Отображение числа с выравниванием вправо */
static void test_show_number(int value)
{
    uint8_t total_digits = display_ll_get_digit_count();
    
    // Очистка буфера перед выводом (заполняем нулями)
    for (int i = 0; i < total_digits; i++) {
        display_ll_set_digit_raw(i, 0x00);
    }

    if (value < 0) value = -value;

    for (int i = 0; i < total_digits; i++) {
        uint8_t digit = (uint8_t)(value % 10);
        value /= 10;
        
        // Позиция справа налево
        uint8_t pos = (uint8_t)(total_digits - 1 - i);
        test_show_digit(pos, digit);

        if (value == 0) break;
    }
}

int main(void)
{
    stdio_init_all();
    
    // Небольшая задержка для инициализации UART/USB
    sleep_ms(2000);
    printf("=== VFD Low-Level Driver Test ===\n");

    // 1. Конфигурация
    display_ll_config_t cfg = {
        .data_pin        = VFD_DATA_PIN,
        .clock_pin       = VFD_CLOCK_PIN,
        .latch_pin       = VFD_LATCH_PIN,
        .digit_count     = VFD_DIGIT_COUNT,
        .refresh_rate_hz = VFD_REFRESH_HZ,
    };

    // 2. Инициализация драйвера
    if (!display_ll_init(&cfg)) {
        printf("ERROR: Display Init Failed!\n");
        // Просто висим в случае ошибки
        while (true) {
            tight_loop_contents();
        }
    }

    // 3. Запуск мультиплексирования
    if (!display_ll_start_refresh()) {
        printf("ERROR: Refresh Start Failed!\n");
        while(true) tight_loop_contents();
    }

    printf("Display Initialized. Running tests...\n");

    // === ТЕСТ 1: Бегущая цифра (проверка адресации сеток) ===
    printf("Test 1: Walking digit\n");
    display_ll_set_brightness_all(100); // Средняя яркость
    
    for (int i = 0; i < VFD_DIGIT_COUNT; i++) {
        // Очистить всё
        for(int k=0; k < VFD_DIGIT_COUNT; k++) display_ll_set_digit_raw(k, 0);
        
        // Показать "8" на текущей позиции
        test_show_digit(i, 8);
        sleep_ms(250);
    }

    // === ТЕСТ 2: Заполнение всех разрядов (проверка Ghosting) ===
    printf("Test 2: Full fill\n");
    for (int i = 0; i < VFD_DIGIT_COUNT; i++) {
        test_show_digit(i, 8);
    }
    sleep_ms(1000);

    // === ТЕСТ 3: Эффект "Дыхание" (проверка PWM и Гаммы) ===
    printf("Test 3: Breathing effect with Counter\n");
    
    uint8_t pwm_val = 0;
    int8_t  pwm_dir = 1;
    int     counter = 0;
    int     tick = 0;

    display_ll_enable_gamma(true); // Включаем гамма-коррекцию

    while (true) {
        // Обновляем число раз в 10 итераций цикла
        if (tick % 10 == 0) {
            test_show_number(counter++);
            if (counter > 9999) counter = 0;
        }

        // Обновляем яркость ("дыхание")
        // Используем apply_gamma для плавности восприятия глазом
        uint8_t corrected_brightness = display_ll_apply_gamma(pwm_val);
        display_ll_set_brightness_all(corrected_brightness);

        // Логика изменения яркости 0 <-> 255
        int next_val = pwm_val + pwm_dir;
        if (next_val >= 255) {
            pwm_val = 255;
            pwm_dir = -1; // Начинаем затухать (быстрее)
        } else if (next_val <= 0) {
            pwm_val = 0;
            pwm_dir = 1;  // Начинаем разгораться
        } else {
            pwm_val = (uint8_t)next_val;
        }

        // Задержка кадра (~60 FPS для анимации дыхания)
        sleep_ms(16);
        tick++;
    }
    
    return 0;
}