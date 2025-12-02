#include "display_font.h"
#include <ctype.h>

/*
 * Модуль шрифтов.
 * Refactor #6: Перевод букв на табличный метод (Lookup Table).
 * 
 * Bit Mapping:
 * Bit 7: DP
 * Bit 6: A, Bit 5: F, Bit 4: E, Bit 3: D, Bit 2: G, Bit 1: B, Bit 0: C
 */

const vfd_segment_map_t g_display_font_digits[10] = {
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

/*
 * Таблица для букв латинского алфавита (A-Z).
 * Индекс = char - 'A'.
 * Символы W и X оставлены пустыми (0x00).
 */
static const vfd_segment_map_t g_display_font_alpha[26] = {
    0x77, // A
    0x1F, // B
    0x78, // C
    0x0F, // D
    0x7C, // E
    0x74, // F
    0x79, // G
    0x37, // H
    0x30, // I
    0x0B, // J
    0x37, // K (как H)
    0x38, // L
    0x55, // M (?)
    0x15, // N
    0x7B, // O
    0x76, // P
    0x73, // Q
    0x14, // R
    0x6D, // S
    0x3C, // T
    0x3B, // U
    0x1C, // V
    0x00, // W (нет маппинга)
    0x00, // X (нет маппинга)
    0x27, // Y
    0x5E  // Z
};

vfd_segment_map_t display_font_get_char(char c)
{
    // Приводим к верхнему регистру для унификации
    if (c >= 'a' && c <= 'z') {
        c = (char)toupper((unsigned char)c);
    }

    // Цифры
    if (c >= '0' && c <= '9') {
        return g_display_font_digits[c - '0'];
    }

    // Буквы A-Z (Табличный метод #6)
    if (c >= 'A' && c <= 'Z') {
        return g_display_font_alpha[c - 'A'];
    }

    // Спецсимволы
    switch (c) {
        case ' ': return 0x00;
        case '-': return 0x04; // G only
        case '_': return 0x08; // D only
        case '.': return 0x80; // DP
        default: return 0x00;
    }
}