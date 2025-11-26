#include "display_api.h"
#include "display_ll.h"

#include <string.h>
#include <stdio.h>

/*
 * CONTENT LAYER
 * -------------
 * Преобразует логические данные в сегментный буфер HL.
 * REAL CHAR_MAP будет добавлен позже, когда перенесём его из legacy.
 */

// Временный локальный CHAR_MAP (как в LL-тесте)
// Полностью заменим позже на сегментную таблицу под твой VFD
static const uint8_t TEMP_NUM_MAP[10] = {
    0b01111011, 0b00000011, 0b01011110, 0b01001111, 0b00100111,
    0b01101101, 0b01111101, 0b01000011, 0b01111111, 0b01101111
};

static inline uint8_t clamp_digit(uint8_t d)
{
    return (d < 10) ? d : 0;
}

/* HL Core буфер setter */
extern void display_core_set_buffer(const vfd_seg_t *buf, uint8_t size);

/* ============================================================
 *     ЧИСЛА
 * ============================================================ */

void display_show_number(int value)
{
    uint8_t buf[VFD_MAX_DIGITS];
    memset(buf, 0, sizeof(buf));

    if (value < 0)
        value = -value;

    // Используем правое выравнивание
    for (int i = 0; i < VFD_MAX_DIGITS; i++) {
        uint8_t digit = (uint8_t)(value % 10);
        value /= 10;

        uint8_t pos = (uint8_t)(VFD_MAX_DIGITS - 1 - i);
        buf[pos] = TEMP_NUM_MAP[digit];
    }

    display_core_set_buffer(buf, VFD_MAX_DIGITS);
}

/* ============================================================
 *     ВРЕМЯ (HH:MM)
 * ============================================================ */

void display_show_time(uint8_t hour, uint8_t minute)
{
    uint8_t buf[VFD_MAX_DIGITS] = {0};

    uint8_t h1 = hour / 10;
    uint8_t h2 = hour % 10;

    uint8_t m1 = minute / 10;
    uint8_t m2 = minute % 10;

    buf[0] = TEMP_NUM_MAP[h1];
    buf[1] = TEMP_NUM_MAP[h2];
    buf[2] = TEMP_NUM_MAP[m1];
    buf[3] = TEMP_NUM_MAP[m2];

    display_core_set_buffer(buf, 4);
}

/* ============================================================
 *   ДАТА (DDMM или MMDD — пока DDMM)
 * ============================================================ */

void display_show_date(uint8_t day, uint8_t month)
{
    uint8_t buf[4];

    buf[0] = TEMP_NUM_MAP[day / 10];
    buf[1] = TEMP_NUM_MAP[day % 10];

    buf[2] = TEMP_NUM_MAP[month / 10];
    buf[3] = TEMP_NUM_MAP[month % 10];

    display_core_set_buffer(buf, 4);
}

/* ============================================================
 *     ТЕКСТ (минимальная версия)
 * ============================================================ */

void display_show_text(const char *text)
{
    uint8_t buf[VFD_MAX_DIGITS] = {0};

    // Пока показываем только цифры, остальные символы игнорируем.
    for (uint8_t i = 0; i < VFD_MAX_DIGITS && text[i]; i++) {
        char c = text[i];
        if (c >= '0' && c <= '9') {
            buf[i] = TEMP_NUM_MAP[c - '0'];
        } else {
            buf[i] = 0; // пусто
        }
    }

    display_core_set_buffer(buf, VFD_MAX_DIGITS);
}
