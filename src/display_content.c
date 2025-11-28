#include "display_api.h"
#include "display_font.h"

#include <string.h>
#include <stdio.h>

extern void display_core_set_buffer(const vfd_seg_t *buf, uint8_t size);

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

    bool negative = (value < 0);
    if (negative) value = -value;

    for (uint8_t i = 0; i < digits; i++) {
        if (value == 0 && i > 0 && !negative) break; // ведущие нули гасим

        uint8_t d = (uint8_t)(value % 10);
        value /= 10;
        
        // Если число кончилось, а надо знак минус
        if (value == 0 && negative) {
            // Рисуем цифру, а в следующем цикле нарисуем минус
            buf[digits - 1 - i] = display_font_digit(d);
            
            // Если есть место под минус
            if (i + 1 < digits) {
                 buf[digits - 1 - (i+1)] = display_font_get_char('-');
            }
            negative = false; // минус отрисован
        } else {
            buf[digits - 1 - i] = display_font_digit(d);
        }
    }
    
    // Если минус остался (число 0)
    if (negative && digits > 0) buf[0] = display_font_get_char('-');

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
        display_show_number((int32_t)(hours * 100 + minutes));
        return;
    }

    buf[0] = display_font_digit(hours / 10);
    buf[1] = display_font_digit(hours % 10);
    buf[2] = display_font_digit(minutes / 10);
    buf[3] = display_font_digit(minutes % 10);

    // Точки: в твоей маске бит 7 = DP. 
    // Если нужно двоеточие, обычно зажигают DP у 2-го разряда (индекс 1)
    if (show_colon) {
        buf[1] |= 0x80; 
    }

    display_core_set_buffer(buf, digits);
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
    
    // Точка разделитель
    buf[1] |= 0x80;

    display_core_set_buffer(buf, digits);
}

/* ============================================================
 *     ТЕКСТ (Теперь поддерживает буквы!)
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
    
    int str_idx = 0;
    int buf_idx = 0;
    
    // Простейший вывод слева направо
    while(text[str_idx] && buf_idx < digits) {
        char c = text[str_idx];
        vfd_seg_t seg = display_font_get_char(c);
        
        // Проверяем точку после символа (например "12.34")
        if (text[str_idx+1] == '.') {
            seg |= 0x80; // Добавляем DP (бит 7)
            str_idx++;   // Пропускаем точку
        }
        
        buf[buf_idx] = seg;
        str_idx++;
        buf_idx++;
    }

    display_core_set_buffer(buf, digits);
}