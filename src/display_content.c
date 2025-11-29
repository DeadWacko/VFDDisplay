#include "display_api.h"
#include "display_font.h"

#include <string.h>
#include <stdio.h>

/*
 * Content Layer.
 * Модуль отвечает за преобразование высокоуровневых типов данных (числа, время, текст)
 * в сырые битовые маски сегментов, используя модуль шрифтов.
 */

/* Внешний интерфейс ядра для обновления буфера */
extern void display_core_set_buffer(const vfd_seg_t *buf, uint8_t size);

/* Получение количества активных разрядов из драйвера нижнего уровня. */
static uint8_t get_active_digits(void)
{
    uint8_t n = display_ll_get_digit_count();
    if (n == 0 || n > VFD_MAX_DIGITS)
        n = VFD_MAX_DIGITS;
    return n;
}

/* ============================================================
 *     ВЫВОД ЧИСЕЛ
 * ============================================================ */

/*
 * Отображение 32-битного целого числа.
 * Особенности:
 * - Выравнивание по правому краю.
 * - Подавление незначащих нулей.
 * - Отображение знака минус для отрицательных чисел.
 */
void display_show_number(int32_t value)
{
    uint8_t digits = get_active_digits();
    vfd_seg_t buf[VFD_MAX_DIGITS];
    memset(buf, 0, sizeof(buf));

    bool negative = (value < 0);
    if (negative) value = -value;

    for (uint8_t i = 0; i < digits; i++) {
        // Прерываем цикл, если число закончилось и ведущие нули не нужны
        if (value == 0 && i > 0 && !negative) break;

        uint8_t d = (uint8_t)(value % 10);
        value /= 10;
        
        // Логика отображения минуса
        if (value == 0 && negative) {
            // Рисуем последнюю цифру
            buf[digits - 1 - i] = display_font_digit(d);
            
            // Если есть свободный разряд слева, рисуем минус
            if (i + 1 < digits) {
                 buf[digits - 1 - (i+1)] = display_font_get_char('-');
            }
            negative = false; // Знак обработан
        } else {
            buf[digits - 1 - i] = display_font_digit(d);
        }
    }
    
    // Граничный случай: минус для нуля или если места не хватило в цикле
    if (negative && digits > 0) buf[0] = display_font_get_char('-');

    display_core_set_buffer(buf, digits);
}

/* ============================================================
 *     ВЫВОД ВРЕМЕНИ
 * ============================================================ */

/*
 * Отображение времени в формате HH:MM.
 * Использует первые 4 разряда.
 * Параметр show_colon включает разделительную точку (бит DP) во втором разряде.
 */
void display_show_time(uint8_t hours, uint8_t minutes, bool show_colon)
{
    uint8_t digits = get_active_digits();
    vfd_seg_t buf[VFD_MAX_DIGITS];
    memset(buf, 0, sizeof(buf));

    if (digits < 4) {
        // Fallback для дисплеев с малым количеством разрядов
        display_show_number((int32_t)(hours * 100 + minutes));
        return;
    }

    buf[0] = display_font_digit(hours / 10);
    buf[1] = display_font_digit(hours % 10);
    buf[2] = display_font_digit(minutes / 10);
    buf[3] = display_font_digit(minutes % 10);

    // Управление разделителем (бит 7 = Decimal Point)
    if (show_colon) {
        buf[1] |= 0x80; 
    }

    display_core_set_buffer(buf, digits);
}

/* ============================================================
 *     ВЫВОД ДАТЫ
 * ============================================================ */

/*
 * Отображение даты в формате DD.MM.
 * Автоматически добавляет точку-разделитель после дня.
 */
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
    
    // Принудительная точка после дня
    buf[1] |= 0x80;

    display_core_set_buffer(buf, digits);
}

/* ============================================================
 *     ВЫВОД ТЕКСТА
 * ============================================================ */

/*
 * Отображение текстовой строки.
 * Поддерживает цифры, латиницу (A-Z) и базовые символы.
 * Точка ('.') в строке привязывается к предыдущему символу (включает DP).
 */
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
    
    // Посимвольный вывод слева направо
    while(text[str_idx] && buf_idx < digits) {
        char c = text[str_idx];
        vfd_seg_t seg = display_font_get_char(c);
        
        // Проверка на следующую точку для слияния символов
        if (text[str_idx+1] == '.') {
            seg |= 0x80; // Включаем DP
            str_idx++;   // Пропускаем символ точки в строке
        }
        
        buf[buf_idx] = seg;
        str_idx++;
        buf_idx++;
    }

    display_core_set_buffer(buf, digits);
}