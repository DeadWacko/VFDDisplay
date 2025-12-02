#include "display_font.h"
#include <ctype.h>

/*
 *  Карта подключения сегментов (Pin Mapping)
 * 
 *  Bit 7: DP (Dot Point)
 *  Bit 6: A
 *  Bit 5: F
 *  Bit 4: E
 *  Bit 3: D
 *  Bit 2: G
 *  Bit 1: B
 *  Bit 0: C
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

vfd_segment_map_t display_font_get_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = (char)toupper((unsigned char)c);
    }

    if (c >= '0' && c <= '9') {
        return g_display_font_digits[c - '0'];
    }

    switch (c) {
        case ' ': return 0x00;
        case '-': return 0x04; // G only
        case '_': return 0x08; // D only
        case '.': return 0x80; // DP

        // Буквы
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
        case 'M': return 0x55; 
        case 'N': return 0x15;
        case 'O': return 0x7B; 
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