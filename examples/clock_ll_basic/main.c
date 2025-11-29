#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/rtc.h"
#include "pico/util/datetime.h"
#include "display_ll.h"

// --- НАСТРОЙКИ ПИНОВ---
#define VFD_DATA_PIN      15
#define VFD_CLOCK_PIN     14
#define VFD_LATCH_PIN     13
#define VFD_DIGIT_COUNT   4
#define VFD_REFRESH_HZ    120

// Бит, отвечающий за точку (DP) или двоеточие
#define SEG_DP            0x80 

/* Таблица сегментов (0-9) */
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

// Хелпер для получения маски цифры
static vfd_seg_t get_digit_mask(uint8_t num) {
    if (num > 9) return 0x00;
    return FONT_DIGITS[num];
}

int main(void) {
    // 1. Инициализация системы
    stdio_init_all();
    sleep_ms(2000); // Ждем USB
    printf("=== VFD Basic Clock Example ===\n");

    // 2. Инициализация RTC
    rtc_init();
    
    // Устанавливаем время по умолчанию: 12:00:00, Вторник, 1 Января 2024
    datetime_t t = {
        .year  = 2024,
        .month = 1,
        .day   = 1,
        .dotw  = 2, // Вторник
        .hour  = 12,
        .min   = 0,
        .sec   = 0
    };
    rtc_set_datetime(&t);
    printf("RTC Initialized to 12:00:00\n");

    // 3. Инициализация Дисплея
    display_ll_config_t cfg = {
        .data_pin        = VFD_DATA_PIN,
        .clock_pin       = VFD_CLOCK_PIN,
        .latch_pin       = VFD_LATCH_PIN,
        .digit_count     = VFD_DIGIT_COUNT,
        .refresh_rate_hz = VFD_REFRESH_HZ,
    };

    if (!display_ll_init(&cfg)) {
        printf("Display Init Error!\n");
        return -1;
    }

    // Устанавливаем яркость
    display_ll_set_brightness_all(200);
    display_ll_start_refresh();

    // 4. Основной цикл
    while (true) {
        // Получаем текущее время
        rtc_get_datetime(&t);

        // Разбиваем часы и минуты на разряды
        // Формат HH:MM (для 4-х разрядного дисплея)
        uint8_t h1 = t.hour / 10;
        uint8_t h0 = t.hour % 10;
        uint8_t m1 = t.min / 10;
        uint8_t m0 = t.min % 10;

        // Определяем, нужно ли зажигать точку/двоеточие (мигаем раз в секунду)
        // Для простоты зажигаем точку на втором разряде (индекс 1)
        bool tick = (t.sec % 2) == 0;
        
        // Формируем маски для вывода
        vfd_seg_t segs[4];
        
        // ВНИМАНИЕ: Порядок разрядов зависит от разводки платы.
        // Предполагаем: [0][1][2][3] -> H1 H0 M1 M0 (Слева направо)
        // Если у вас наоборот - поменяйте индексы.
        
        segs[0] = get_digit_mask(h1);
        segs[1] = get_digit_mask(h0);
        segs[2] = get_digit_mask(m1);
        segs[3] = get_digit_mask(m0);

        // Добавляем мигающую точку к разряду часов (чтобы имитировать двоеточие)
        if (tick) {
            segs[1] |= SEG_DP; 
        }

        // Загружаем в дисплей
        for(int i=0; i<4; i++) {
            display_ll_set_digit_raw(i, segs[i]);
        }

        // Обновляем не слишком часто, 10 раз в секунду достаточно для часов
        sleep_ms(100); 
    }
}