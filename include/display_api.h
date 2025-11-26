#ifndef DISPLAY_API_H
#define DISPLAY_API_H

#include <stdint.h>
#include <stdbool.h>
#include "display_ll.h"

/*
 * HIGH-LEVEL API
 * --------------
 * Предназначен для использования в приложениях:
 * часы, индикаторы, проекты на Pico.
 *
 * Включает:
 * - отображение числа
 * - отображение текста
 * - время
 * - эффекты (wave, pulse, fade, glitch)
 * - оверлеи (WiFi, NTP sync, Boot)
 *
 * Вся логика работает поверх LL-API.
 */

/* Режим отображения */
typedef enum {
    DISPLAY_MODE_CONTENT = 0,     // обычное содержимое (время/числа)
    DISPLAY_MODE_EFFECT,          // эффект полностью заменяет картинку
    DISPLAY_MODE_OVERLAY          // оверлей поверх content
} display_mode_t;

/* Инициализация High-Level API (инициализирует LL внутри себя) */
void display_init(uint8_t digit_count);

/* =====================
 *  ОТРИСОВКА КОНТЕНТА
 * ===================== */

/* Показать число с автоматическим заполнением нулями */
void display_show_number(int32_t value);

/* Показать текст (до 10 символов) */
void display_show_text(const char *text);

/* Показать время: HHMM или HH:MM */
void display_show_time(uint8_t hours, uint8_t minutes, bool show_colon);

/* Показать дату DDMM или DD.MM */
void display_show_date(uint8_t day, uint8_t month);

/* Получить доступ к текущему CONTENT-буферу */
vfd_seg_t *display_content_buffer(void);

/* =====================
 *        ЭФФЕКТЫ
 * ===================== */

/* Волна яркости */
bool display_fx_wave(uint32_t duration_ms);

/* Пульс (дыхание) */
bool display_fx_pulse(uint32_t duration_ms);

/* Плавное появление */
bool display_fx_fade_in(uint32_t duration_ms);

/* Плавное исчезновение */
bool display_fx_fade_out(uint32_t duration_ms);

/* Глитч / flicker */
bool display_fx_glitch(uint32_t duration_ms);

/* "Matrix rain" */
bool display_fx_matrix(uint32_t duration_ms, uint32_t frame_ms);

/* Остановить любой эффект */
void display_fx_stop(void);

/* =====================
 *       ОВЕРЛЕИ
 * ===================== */

/* Мягкая анимация при загрузке */
bool display_overlay_boot(uint32_t duration_ms);

/* WiFi-аутентификация / подключение */
bool display_overlay_wifi(uint32_t duration_ms);

/* Синхронизация времени по NTP */
bool display_overlay_ntp(uint32_t duration_ms);

/* Остановить оверлей и вернуть нормальное отображение */
void display_overlay_stop(void);

/* =====================
 *      ЯРКОСТЬ
 * ===================== */

/* Общая яркость */
void display_set_brightness(uint8_t value);

/* Ночной режим */
void display_set_night_mode(bool enable);

/* Автоприглушение (будет использовать датчик) */
void display_set_auto_brightness(bool enable);

/* =====================
 *        СТАТУС
 * ===================== */

/* Текущий режим */
display_mode_t display_get_mode(void);

/* Идёт ли анимация? */
bool display_is_effect_running(void);
bool display_is_overlay_running(void);

#endif // DISPLAY_API_H
