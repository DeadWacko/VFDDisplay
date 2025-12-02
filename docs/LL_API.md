# Low-Level Layer — API Reference

**Платформа:** RP2040  
**Версия:** `v1.3.7`

---

## 1. Аппаратная Конфигурация (Hardware Setup)

Библиотека рассчитана на управление VFD/LED дисплеями через сдвиговые регистры **74HC595**.

### Схема подключения (Daisy Chain)
Данные передаются последовательно через каскад регистров.

`MCU (RP2040)  ->  [74HC595: GRID]  ->  [74HC595: SEGMENTS]`

1.  **Grid Register (Первый в цепочке):** Управляет сетками (разрядами).
    *   Q0 -> Grid 1
    *   Q1 -> Grid 2
    *   ...
2.  **Segment Register (Последний в цепочке):** Управляет сегментами (анодами).

*Примечание: Если разрядов больше 8, используется два регистра для сеток.*

### Segment Bit Layout (Issue #3)
Тип данных `vfd_segment_map_t` (uint8_t) имеет следующую структуру.
Это стандартная разводка 7-сегментного индикатора с точкой.

| Bit | Name | Description | Position |
|:---:|:---:|:---|:---|
| **7** | **DP** | Decimal Point | Точка (обычно справа внизу) |
| **6** | **A** | Top | Верхняя планка |
| **5** | **F** | Top-Left | Верхняя левая |
| **4** | **E** | Bottom-Left | Нижняя левая |
| **3** | **D** | Bottom | Нижняя планка |
| **2** | **G** | Middle | Средняя планка |
| **1** | **B** | Top-Right | Верхняя правая |
| **0** | **C** | Bottom-Right | Нижняя правая |

```
      A (6)
     -------
    |       |
F(5)|       | B(1)
    | G (2) |
     -------
    |       |
E(4)|       | C(0)
    |       |
     -------  O DP(7)
      D (3)
```

---

## 2. API Reference

### Инициализация

#### `void display_ll_init(const display_ll_config_t *cfg)`
Инициализирует драйвер с пользовательской распиновкой.

```c
typedef struct {
    uint8_t data_pin;   // DS (SER)
    uint8_t clock_pin;  // SH_CP (SRCLK)
    uint8_t latch_pin;  // ST_CP (RCLK)
    uint8_t digit_count;
    uint16_t refresh_rate_hz;
} display_ll_config_t;
```

---

### Рендеринг

#### `void display_ll_set_digit_raw(uint8_t idx, vfd_segment_map_t segments)`
Записывает паттерн сегментов в буфер.
*   **Debug:** Вызывает `assert`, если `idx` вне диапазона.
*   **Release:** Безопасно игнорирует некорректный индекс.

#### `void display_ll_set_brightness(uint8_t idx, uint8_t level)`
Устанавливает яркость (PWM) для конкретного разряда.

---

### Utility

#### `void display_ll_set_brightness_all(uint8_t level)`
Глобальная установка яркости (атомарно).

#### `vfd_segment_map_t *display_ll_get_buffer(void)`
Доступ к "сырому" видеобуферу.