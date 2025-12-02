#include "display_api.h"
#include "display_font.h"

#include <string.h>
#include <stdio.h>

/*
 * Content Layer.
 * Модуль отвечает за преобразование высокоуровневых типов данных.
 *
 * FIX #25: Безопасная обработка INT32_MIN через uint32_t во избежание UB и OOB-чтения.
 */

extern void display_core_set_buffer(const vfd_segment_map_t *buf, uint8_t size);

static uint8_t get_active_digits(void)
{
    uint8_t n = display_ll_get_digit_count();
    if (n == 0 || n > VFD_MAX_DIGITS) n = VFD_MAX_DIGITS;
    return n;
}

/* ============================================================
 *     ВЫВОД ЧИСЕЛ
 * ============================================================ */

void display_show_number(int32_t value)
{
    uint8_t digits = get_active_digits();
    vfd_segment_map_t buf[VFD_MAX_DIGITS];
    memset(buf, 0, sizeof(buf));

    bool negative = (value < 0);
    
    // FIX #25: Используем uint32_t для модуля числа.
    // Это предотвращает UB при value == INT32_MIN (-2147483648),
    // так как -INT32_MIN не влезает в int32_t, но влезает в uint32_t.
    // Трюк -(uint32_t)value корректно переворачивает биты для 2's complement.
    uint32_t abs_val = (negative) ? (0u - (uint32_t)value) : (uint32_t)value;

    for (uint8_t i = 0; i < digits; i++) {
        // Проверяем abs_val вместо value
        if (abs_val == 0 && i > 0 && !negative) break;

        uint8_t d = (uint8_t)(abs_val % 10);
        abs_val /= 10;
        
        if (abs_val == 0 && negative) {
            buf[digits - 1 - i] = display_font_digit(d);
            if (i + 1 < digits) {
                 buf[digits - 1 - (i+1)] = display_font_get_char('-');
            }
            negative = false;
        } else {
            buf[digits - 1 - i] = display_font_digit(d);
        }
    }
    
    if (negative && digits > 0) buf[0] = display_font_get_char('-');

    display_core_set_buffer(buf, digits);
}

/* ============================================================
 *     ВЫВОД ВРЕМЕНИ
 * ============================================================ */

void display_show_time(uint8_t hours, uint8_t minutes, bool show_colon)
{
    (void)show_colon; 

    uint8_t digits = get_active_digits();
    vfd_segment_map_t buf[VFD_MAX_DIGITS];
    memset(buf, 0, sizeof(buf));

    if (digits < 4) {
        display_show_number((int32_t)(hours * 100 + minutes));
        return;
    }

    buf[0] = display_font_digit(hours / 10);
    buf[1] = display_font_digit(hours % 10);
    buf[2] = display_font_digit(minutes / 10);
    buf[3] = display_font_digit(minutes % 10);

    display_core_set_buffer(buf, digits);
}

/* ============================================================
 *     ВЫВОД ДАТЫ
 * ============================================================ */

void display_show_date(uint8_t day, uint8_t month)
{
    uint8_t digits = get_active_digits();
    vfd_segment_map_t buf[VFD_MAX_DIGITS];
    memset(buf, 0, sizeof(buf));

    if (digits < 4) {
        display_show_number((int32_t)(day * 100 + month));
        return;
    }

    buf[0] = display_font_digit(day / 10);
    buf[1] = display_font_digit(day % 10);
    buf[2] = display_font_digit(month / 10);
    buf[3] = display_font_digit(month % 10);
    
    display_core_set_buffer(buf, digits);
}

/* ============================================================
 *     ВЫВОД ТЕКСТА
 * ============================================================ */

void display_show_text(const char *text)
{
    uint8_t digits = get_active_digits();
    vfd_segment_map_t buf[VFD_MAX_DIGITS];
    memset(buf, 0, sizeof(buf));

    if (!text) {
        display_core_set_buffer(buf, digits);
        return;
    }
    
    int str_idx = 0;
    int buf_idx = 0;
    
    while(text[str_idx] && buf_idx < digits) {
        char c = text[str_idx];
        vfd_segment_map_t seg = display_font_get_char(c);
        
        if (text[str_idx+1] == '.') {
            seg |= 0x80; 
            str_idx++;
        }
        
        buf[buf_idx] = seg;
        str_idx++;
        buf_idx++;
    }

    display_core_set_buffer(buf, digits);
}