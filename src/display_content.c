#include "display_api.h"
#include "display_font.h"

#include <string.h>
#include <stdio.h>

/*
 * CONTENT LAYER
 * -------------
 * Преобразует логические данные (числа, время, дату, текст)
 * в сегментный буфер HL и отдаёт его ядру.
 *
 * CHAR_MAP (шрифт) лежит в display_font.[ch].
 */

extern void display_core_set_buffer(const vfd_seg_t *buf, uint8_t size);

/* Аккуратный helper: получить фактическое число разрядов от LL */
static uint8_t get_active_digits(void)
{
    uint8_t n = display_ll_get_digit_count();
    if (n == 0 || n > VFD_MAX_DIGITS)
        n = VFD_MAX_DIGITS;
    return n;
}

/* ============================================================
 *     ЧИСЛА
 * ============================================================ */

void display_show_number(int32_t value)
{
    uint8_t digits = get_active_digits();
    vfd_seg_t buf[VFD_MAX_DIGITS];
    memset(buf, 0, sizeof(buf));

    if (value < 0) {
        // TODO: когда появится символ "-", можно будет его реально рисовать
        value = -value;
    }

    // Правое выравнивание
    for (uint8_t i = 0; i < digits; i++) {
        uint8_t d = (uint8_t)(value % 10);
        value /= 10;

        uint8_t pos = (uint8_t)(digits - 1 - i);
        buf[pos] = display_font_digit(d);
    }

    display_core_set_buffer(buf, digits);
}

/* ============================================================
 *     ВРЕМЯ (HH:MM)
 * ============================================================ */

void display_show_time(uint8_t hours, uint8_t minutes, bool show_colon)
{
    uint8_t digits = get_active_digits();
    vfd_seg_t buf[VFD_MAX_DIGITS];
    memset(buf, 0, sizeof(buf));

    if (digits < 4) {
        // Если вдруг меньше 4 разрядов — выводим только младшие цифры
        display_show_number((int32_t)(hours * 100 + minutes));
        return;
    }

    uint8_t h1 = hours / 10;
    uint8_t h2 = hours % 10;
    uint8_t m1 = minutes / 10;
    uint8_t m2 = minutes % 10;

    buf[0] = display_font_digit(h1);
    buf[1] = display_font_digit(h2);
    buf[2] = display_font_digit(m1);
    buf[3] = display_font_digit(m2);

    // Точечки — грубо: включаем DP на середине (бит 7)
    if (show_colon) {
        buf[1] |= 0b10000000;
        
    }

    display_core_set_buffer(buf, 4);
}

/* ============================================================
 *   ДАТА (DDMM)
 * ============================================================ */

void display_show_date(uint8_t day, uint8_t month)
{
    uint8_t digits = get_active_digits();
    vfd_seg_t buf[VFD_MAX_DIGITS];
    memset(buf, 0, sizeof(buf));

    if (digits < 4) {
        display_show_number((int32_t)(day * 100 + month));
        return;
    }

    buf[0] = display_font_digit(day / 10);
    buf[1] = display_font_digit(day % 10);
    buf[2] = display_font_digit(month / 10);
    buf[3] = display_font_digit(month % 10);

    display_core_set_buffer(buf, 4);
}

/* ============================================================
 *     ТЕКСТ (минимум — цифры)
 * ============================================================ */

void display_show_text(const char *text)
{
    uint8_t digits = get_active_digits();
    vfd_seg_t buf[VFD_MAX_DIGITS];
    memset(buf, 0, sizeof(buf));

    if (!text) {
        display_core_set_buffer(buf, digits);
        return;
    }

    for (uint8_t i = 0; i < digits && text[i]; i++) {
        char c = text[i];
        if (c >= '0' && c <= '9') {
            buf[i] = display_font_digit((uint8_t)(c - '0'));
        } else {
            buf[i] = 0; // Пока все не-цифры — пусто
        }
    }

    display_core_set_buffer(buf, digits);
}
