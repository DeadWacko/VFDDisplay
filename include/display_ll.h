#ifndef DISPLAY_LL_H
#define DISPLAY_LL_H

#include <stdint.h>
#include <stdbool.h>

/*
 * LOW-LEVEL API
 * -------------
 * Этот слой отвечает только за "железо":
 * - буфер сырых сегментов (8 бит = маска сегментов)
 * - яркость по разрядам (0..VFD_MAX_BRIGHTNESS)
 * - мультиплексирование разрядов
 * - программный PWM по яркости
 * - гамма-коррекция яркости
 *
 * Здесь НЕТ понятий "время", "эффект", "оверлей" и т.д.
 */

#define VFD_MAX_DIGITS      10
#define VFD_MAX_BRIGHTNESS  255

/* Сырой образ разряда: 1 бит = один сегмент */
typedef uint8_t vfd_seg_t;

/* Конфигурация LL-слоя */
typedef struct {
    uint8_t data_pin;          // GPIO данных в сдвиговый регистр
    uint8_t clock_pin;         // GPIO такта сдвигового регистра
    uint8_t latch_pin;         // GPIO защёлки (STCP / RCLK)
    uint8_t digit_count;       // количество разрядов (1..VFD_MAX_DIGITS)
    uint16_t refresh_rate_hz;  // частота обновления по разрядам (100–200 Гц)
} display_ll_config_t;

/* =====================
 *     ИНИЦИАЛИЗАЦИЯ
 * ===================== */

/**
 * Инициализация LL-слоя.
 * Настраивает пины, очищает буферы, но НЕ запускает refresh.
 *
 * @return true при успехе, false при неверной конфигурации.
 */
bool display_ll_init(const display_ll_config_t *cfg);

/** LL инициализирован? */
bool display_ll_is_initialized(void);

/* Запустить/остановить фоновое мультиплексирование разрядов. */
bool display_ll_start_refresh(void);
void display_ll_stop_refresh(void);

/* =====================
 *      БУФЕР СЕГМЕНТОВ
 * ===================== */

/** Количество разрядов, с которым инициализирован LL. */
uint8_t display_ll_get_digit_count(void);

/**
 * Прямой доступ к буферу сегментов.
 * Каждый элемент — сырая 8-битовая маска сегментов для разряда.
 */
vfd_seg_t *display_ll_get_buffer(void);

/** Установить маску сегментов одного разряда. */
void display_ll_set_digit_raw(uint8_t index, vfd_seg_t segmask);

/* =====================
 *        ЯРКОСТЬ
 * ===================== */

/** Установить яркость конкретного разряда (0..VFD_MAX_BRIGHTNESS). */
void display_ll_set_brightness(uint8_t index, uint8_t level);

/** Установить одинаковую яркость для всех разрядов. */
void display_ll_set_brightness_all(uint8_t level);

/* =====================
 *    ГАММА-КОРРЕКЦИЯ
 * ===================== */

/**
 * Применить гамма-коррекцию к линейному уровню (0..255).
 * Функция не изменяет внутреннее состояние и может свободно
 * использоваться снаружи.
 */
uint8_t display_ll_apply_gamma(uint8_t linear);

/** Включить/выключить встроенную гамма-коррекцию яркости. */
void display_ll_enable_gamma(bool enable);

#endif // DISPLAY_LL_H
