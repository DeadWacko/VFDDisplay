# VFD Display Library — Architecture Overview

**Версия:** `v1.3.7`
**Статус:** `stable`

Проект построен по строгой многослойной архитектуре, обеспечивающей изоляцию аппаратной части от бизнес-логики.

```
+--------------------------+
|   APPLICATION (Main)     | ← Пользовательский код
+--------------------------+
|       API LAYER          | ← display_api.h (Фасад)
+--------------------------+
|    HIGH-LEVEL CORE       | ← State Machine, FX Engine, Logic
+--------------------------+
|    LOW-LEVEL DRIVER      | ← Hardware Abstraction, PWM, IRQ
+--------------------------+
```

---

## 1. Low-Level Layer (LL)
**Файлы:** `display_ll.c`, `display_ll.h`

Автономный драйвер управления железом.
- **Развертка:** `repeating_timer`, поддержка 1-16 разрядов.
- **PWM:** `hardware_alarm`, 8 бит яркости на каждый разряд.
- **Safety:** Атомарные операции с буфером, защита от индексов вне диапазона (Asserts).
- **Gamma:** Аппаратная коррекция ($x^2$).

Подробности API: [LL_API.md](./LL_API.md)

---

## 2. High-Level Core (HL)
**Файлы:** `display_core.c`, `display_state.h`

Центральный хаб (`g_display`).
- **State Machine:** Управление режимами (Content / Effect / Overlay).
- **Router:** Слияние контента с маской системных точек (`dots_map`).
- **Brightness:** Расчет автояркости с учетом лимитов для активных эффектов.

**Стек приоритетов (Pipeline):**
1. **OVERLAY** (Высший приоритет: Уведомления)
2. **BLOCKING FX** (Захват сегментов: Glitch, Text, Morph)
3. **TRANSPARENT FX** (Модуляция яркости: Pulse, Wave)
4. **CONTENT** (Базовый слой: Время, Числа)

---

## 3. FX Engine
**Файлы:** `display_fx.c`

Движок процедурных анимаций.
Подробная документация и классификация: [Effects_System.md](./Effects_System.md).

---

## 4. Content Layer
**Файлы:** `display_content.c`, `display_font.c`

Слой представления данных.
- **Font Mapping:** Табличная конвертация ASCII -> Segments.
- **Rendering:** Форматирование чисел и времени. Игнорирует системные разделители (они накладываются в Core).

---

## 5. Цикл обновления (Heartbeat)

Функция `display_process()` (вызывается в `while(1)`):
1. **Auto-Brightness:** Обновление глобальной яркости (если нет оверлея).
2. **Overlay Check:** Рендер системных уведомлений.
3. **FX Tick:** Расчет текущего кадра эффекта.
   - Если эффект блокирующий → прерывание обновления контента.
4. **Content & Dots:** Слияние буфера контента с маской разделителей (`dots_map`).
5. **Push:** Отправка в LL.