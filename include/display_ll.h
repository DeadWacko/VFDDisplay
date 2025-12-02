#ifndef DISPLAY_LL_H
#define DISPLAY_LL_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Low-Level API.
 * Слой аппаратной абстракции. Отвечает за мультиплексирование,
 * управление GPIO (сдвиговые регистры), программный PWM и гамма-коррекцию.
 */

#define VFD_MAX_DIGITS      10
#define VFD_MAX_BRIGHTNESS  255

/* 
 * Тип данных для карты сегментов (8 бит).
 * Каждый бит соответствует состоянию сегмента (A-G, DP).
 * См. LL_API.md для описания битовой раскладки.
 */
typedef uint8_t vfd_segment_map_t;

/* Конфигурация драйвера низкого уровня. */
typedef struct {
    uint8_t data_pin;          // GPIO: Data (DS)
    uint8_t clock_pin;         // GPIO: Clock (SH_CP)
    uint8_t latch_pin;         // GPIO: Latch (ST_CP)
    uint8_t digit_count;       // Количество разрядов (1..VFD_MAX_DIGITS)
    uint16_t refresh_rate_hz;  // Частота обновления экрана (рек. 100-120 Гц)
} display_ll_config_t;

/* =====================
 *     ИНИЦИАЛИЗАЦИЯ
 * ===================== */

/*
 * Инициализация драйвера, настройка GPIO и выделение ресурсов.
 * Возвращает true при успешной инициализации.
 */
bool display_ll_init(const display_ll_config_t *cfg);

/* Деинициализация драйвера, остановка таймеров и освобождение ресурсов. */
void display_ll_deinit(void);

/* Проверка статуса инициализации драйвера. */
bool display_ll_is_initialized(void);

/* Запуск фонового процесса обновления дисплея (мультиплексирования). */
bool display_ll_start_refresh(void);

/* Остановка фонового процесса обновления. */
void display_ll_stop_refresh(void);

/* =====================
 *   БУФЕР И СЕГМЕНТЫ
 * ===================== */

/* Получение количества сконфигурированных разрядов. */
uint8_t display_ll_get_digit_count(void);

/* Получение указателя на сырой буфер сегментов. */
vfd_segment_map_t *display_ll_get_buffer(void);

/* Установка паттерна сегментов для указанного разряда. */
void display_ll_set_digit_raw(uint8_t index, vfd_segment_map_t segments);

/* =====================
 *        ЯРКОСТЬ
 * ===================== */

/* Установка яркости для конкретного разряда (0..255). */
void display_ll_set_brightness(uint8_t index, uint8_t level);

/* Установка одинаковой яркости для всех разрядов (0..255). */
void display_ll_set_brightness_all(uint8_t level);

/* =====================
 *    ГАММА-КОРРЕКЦИЯ
 * ===================== */

/* Преобразование линейного значения яркости в скорректированное (Gamma). */
uint8_t display_ll_apply_gamma(uint8_t linear);

/* Включение или выключение автоматической гамма-коррекции. */
void display_ll_enable_gamma(bool enable);

#endif // DISPLAY_LL_H