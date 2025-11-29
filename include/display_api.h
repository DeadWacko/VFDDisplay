#ifndef DISPLAY_API_H
#define DISPLAY_API_H

#include <stdint.h>
#include <stdbool.h>
#include "display_ll.h"

/*
 * High-Level API.
 * Предоставляет функции для управления контентом, эффектами и режимами дисплея.
 */

/* Режим работы дисплея */
typedef enum {
    DISPLAY_MODE_CONTENT = 0,     // Отображение основного контента
    DISPLAY_MODE_EFFECT,          // Активен визуальный эффект
    DISPLAY_MODE_OVERLAY          // Активен оверлей (уведомление)
} display_mode_t;

/* Инициализация подсистемы дисплея и драйвера низкого уровня. */
void display_init(uint8_t digit_count);

/* =====================
 *  ОТРИСОВКА КОНТЕНТА
 * ===================== */

/* Вывод целого числа с выравниванием по правому краю. */
void display_show_number(int32_t value);

/* Вывод текстовой строки. */
void display_show_text(const char *text);

/* Вывод времени в формате HH:MM. */
void display_show_time(uint8_t hours, uint8_t minutes, bool show_colon);

/* Вывод даты в формате DD.MM. */
void display_show_date(uint8_t day, uint8_t month);

/* Получение указателя на текущий буфер контента. */
vfd_seg_t *display_content_buffer(void);

/* =====================
 *  ТЕКСТОВЫЕ ЭФФЕКТЫ
 * ===================== */

/* Запуск эффекта бегущей строки (Marquee). */
bool display_fx_marquee(const char *text, uint32_t speed_ms);

/* Запуск эффекта выезда текста справа (Slide In). */
bool display_fx_slide_in(const char *text, uint32_t speed_ms);

/* =====================
 *  ВИЗУАЛЬНЫЕ ЭФФЕКТЫ
 * ===================== */

/* Запуск эффекта "Волна яркости". */
bool display_fx_wave(uint32_t duration_ms);

/* Запуск эффекта "Пульсация" (дыхание). */
bool display_fx_pulse(uint32_t duration_ms);

/* Запуск плавного появления (Fade In). */
bool display_fx_fade_in(uint32_t duration_ms);

/* Запуск плавного затухания (Fade Out). */
bool display_fx_fade_out(uint32_t duration_ms);

/* Запуск эффекта "Глитч" (цифровой шум). */
bool display_fx_glitch(uint32_t duration_ms);

/* Обновление состояния текущего эффекта (внутренний вызов). */
void display_fx_tick(void);

/* Запуск эффекта "Сканер" (Larson Scanner). */
bool display_fx_matrix(uint32_t duration_ms, uint32_t frame_ms);

/* Запуск эффекта морфинга (плавное превращение). */
bool display_fx_morph(uint32_t duration_ms, const vfd_seg_t *target, uint32_t steps);

/* Запуск эффекта рассыпания (Dissolve). */
bool display_fx_dissolve(uint32_t duration_ms);

/* Принудительная остановка текущего эффекта. */
void display_fx_stop(void);

/* =====================
 *       ОВЕРЛЕИ
 * ===================== */

/* Запуск анимации загрузки. */
bool display_overlay_boot(uint32_t duration_ms);

/* Запуск анимации ожидания Wi-Fi. */
bool display_overlay_wifi(uint32_t duration_ms);

/* Запуск анимации синхронизации NTP. */
bool display_overlay_ntp(uint32_t duration_ms);

/* Остановка оверлея и возврат к контенту. */
void display_overlay_stop(void);

/* =====================
 *      НАСТРОЙКИ
 * ===================== */

/* Установка глобального уровня яркости (0-255). */
void display_set_brightness(uint8_t value);

/* Включение или выключение ночного режима. */
void display_set_night_mode(bool enable);

/* Включение или выключение автоматической яркости (по датчику). */
void display_set_auto_brightness(bool enable);

/* Управление автоматическим миганием разделительных точек. */
void display_set_dot_blinking(bool enable);

/* =====================
 *        СТАТУС
 * ===================== */

/* Получение текущего режима работы. */
display_mode_t display_get_mode(void);

/* Проверка активности эффекта. */
bool display_is_effect_running(void);

/* Проверка активности оверлея. */
bool display_is_overlay_running(void);

/* =====================
 *   ГЛАВНЫЙ ЦИКЛ
 * ===================== */

/*
 * Основная функция обработки.
 * Выполняет обновление анимаций, яркости и логики дисплея.
 * Должна вызываться периодически в главном цикле.
 */
void display_process(void);

#endif // DISPLAY_API_H