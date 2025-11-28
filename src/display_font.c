#include "display_font.h"
#include <ctype.h>

/*
 * Твоя таблица цифр (Pin Mapping из твоего проекта)
 * 
 * Bit 6 = A, Bit 5 = F, Bit 4 = E, Bit 3 = D, 
 * Bit 2 = G, Bit 1 = B, Bit 0 = C, Bit 7 = DP
 */
const vfd_seg_t g_display_font_digits[10] = {
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
 * Функция получения символа (только реализация get_char, 
 * так как get_digit уже в хедере как inline)
 */
vfd_seg_t display_font_get_char(char c)
{
    // Приводим к верхнему регистру
    if (c >= 'a' && c <= 'z') {
        c = (char)toupper((unsigned char)c);
    }

    // Если это цифра — берем из массива
    if (c >= '0' && c <= '9') {
        return g_display_font_digits[c - '0'];
    }

    // Буквы и символы (рассчитаны под твою распиновку)
    switch (c) {
        case ' ': return 0x00;
        case '-': return 0x04; // G only (Bit 2)
        case '_': return 0x08; // D only (Bit 3)
        case '.': return 0x80; // DP (Bit 7)

        // Буквы (Bit mapping: A=6, B=1, C=0, D=3, E=4, F=5, G=2)
        case 'A': return 0x77; 
        case 'B': return 0x1F; 
        case 'C': return 0x78; 
        case 'D': return 0x0F; 
        case 'E': return 0x7C; 
        case 'F': return 0x74; 
        case 'G': return 0x79; 
        case 'H': return 0x37; 
        case 'I': return 0x30; 
        case 'J': return 0x0B;
        case 'K': return 0x37; 
        case 'L': return 0x38; 
        case 'M': return 0x55; // n-образная
        case 'N': return 0x15; // n
        case 'O': return 0x7B; // 0
        case 'P': return 0x76;
        case 'Q': return 0x73;
        case 'R': return 0x14; 
        case 'S': return 0x6D; 
        case 'T': return 0x3C; 
        case 'U': return 0x3B;
        case 'V': return 0x1C; 
        case 'Y': return 0x27; 
        case 'Z': return 0x5E; 

        default: return 0x00;
    }
}